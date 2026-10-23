#include "NasService.h"
#include "StorageService.h"
#include "CredentialStore.h"
#include "NetworkService.h"
#include "PowerManager.h"
#include "SecurityService.h"
#include "../core/EventLog.h"
#include "../core/EventBus.h"
#include "../core/PsramArray.h"
#include <smb2/libsmb2.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/smb2-errors.h>
#include "SmbDirectoryPage.h"
#include <sys/poll.h>
#include <new>
#include <fcntl.h>
#include <freertos/task.h>
#include <memory>
#include <algorithm>

namespace hp {

static constexpr const char* kNasProfilesPath = "/nas/profiles.bin";


NasService& NasService::instance() { static NasService n; return n; }
bool NasService::copyProfile(size_t index,NasProfile& out)const{
    portENTER_CRITICAL(&profilesMux_);const bool exists=profiles_&&index<profileCount_;
    if(exists)out=profiles_[index];portEXIT_CRITICAL(&profilesMux_);return exists;
}
int NasService::providerStatus(uint32_t id)const{
    int result=-1;portENTER_CRITICAL(&profilesMux_);
    for(size_t i=0;i<profileCount_;++i)if(profiles_[i].id==id){const uint8_t bit=1u<<i;if(providerKnown_&bit)result=(providerOnline_&bit)?1:0;break;}
    portEXIT_CRITICAL(&profilesMux_);return result;
}
void NasService::reportProvider(uint32_t id,bool online){bool changed=false;
    portENTER_CRITICAL(&profilesMux_);for(size_t i=0;i<profileCount_;++i)if(profiles_[i].id==id){const uint8_t bit=1u<<i;
        changed=!(providerKnown_&bit)||bool(providerOnline_&bit)!=online;providerKnown_|=bit;
        if(online)providerOnline_|=bit;else providerOnline_&=~bit;break;}portEXIT_CRITICAL(&profilesMux_);
    if(changed){++contentRevision_;StorageProviderEvent status{id,online};EventBus::instance().publish({EventType::StorageProviderChanged,&status});}
}

bool NasService::begin() {
    profiles_ = allocatePsramArray<NasProfile>(kMaxProfiles);
    browseEntries_ = allocatePsramArray<NasBrowseEntry>(kMaxBrowseEntries);
    if (!profiles_ || !browseEntries_) return false;
    lock_ = xSemaphoreCreateMutex();
    browseLock_ = xSemaphoreCreateMutex();
    if (!lock_ || !browseLock_) return false;
    return load();
}

bool NasService::load() {
    profileCount_ = 0;
    auto& st = StorageService::instance();
    fs::FS* fs = st.fsFor(StorageClass::UserData);
    if (!fs) return true;
    File f = fs->open(st.pathFor(StorageClass::UserData,kNasProfilesPath),FILE_READ);
    if (!f) return true;
    while (profileCount_ < kMaxProfiles && f.available() >= (int)sizeof(NasProfile)) {
        NasProfile p{};
        if (f.read((uint8_t*)&p,sizeof(p)) != sizeof(p)) break;
        if (p.id && p.name[0] && p.host[0]) profiles_[profileCount_++] = p;
    }
    f.close();
    return true;
}

bool NasService::save() {
    return StorageService::instance().writeBinaryAtomic(
        StorageClass::UserData,kNasProfilesPath,profiles_,profileCount_*sizeof(NasProfile));
}

bool NasService::addProfile(const NasProfile& input,const String& password) {
    ++contentRevision_;
    if (profileCount_ >= kMaxProfiles || !input.name[0] || !input.host[0]) return false;
    NasProfile p = input;
    if (!p.id) {
        uint32_t h=2166136261u;
        for (const char* s : {p.name,p.host,p.path})
            for (const char* c=s; c && *c; ++c) { h^=(uint8_t)*c; h*=16777619u; }
        p.id = h ? h : 1;
    }
    portENTER_CRITICAL(&profilesMux_);profiles_[profileCount_++] = p;portEXIT_CRITICAL(&profilesMux_);
    if (!save()) { portENTER_CRITICAL(&profilesMux_);--profileCount_;portEXIT_CRITICAL(&profilesMux_);return false; }
    if (!CredentialStore::instance().putNasPassword(p.id,password)) {
        portENTER_CRITICAL(&profilesMux_);--profileCount_;portEXIT_CRITICAL(&profilesMux_);save();return false;
    }
    return true;
}

bool NasService::removeProfile(size_t index) {
    ++contentRevision_;
    if (index >= profileCount_) return false;
    CredentialStore::instance().removeNasPassword(profiles_[index].id);
    portENTER_CRITICAL(&profilesMux_);
    for (size_t i=index+1;i<profileCount_;++i) profiles_[i-1]=profiles_[i];
    --profileCount_; profiles_[profileCount_]={};
    providerKnown_=providerOnline_=0;
    portEXIT_CRITICAL(&profilesMux_);
    return save();
}



struct NasService::NasJob {
    NasService* owner = nullptr;
    NasProfile profile{};
    uint8_t profileIndex = 0;
    String folder;  // Share-relative directory; never a URL.
    String file;    // Share-relative file, if downloading.
    bool download = false;
};

// No traversal, absolute paths, control bytes or special network separators.
static bool smbSafeRelative(const String& relative) {
    if (relative.length() > 180 || relative.indexOf('\\') >= 0 ||
        relative.indexOf(':') >= 0) return false;
    String rest = relative;
    if (rest.startsWith("/")) rest.remove(0, 1);
    while (rest.length()) {
        int sep = rest.indexOf('/');
        String component = sep < 0 ? rest : rest.substring(0, sep);
        if (component == "." || component == "..") return false;
        for (size_t i = 0; i < component.length(); ++i)
            if (static_cast<uint8_t>(component[i]) < 32) return false;
        if (sep < 0) break;
        rest.remove(0, sep + 1);
    }
    return true;
}

// NasProfile.path stores "SHARE" or "SHARE/subdirectory" for SMB. The
// server name stays independent so Unraid and FRITZ!Box need no hardcoding.
static bool smbSplitShare(const NasProfile& profile, String& share, String& prefix) {
    String path = String(profile.path);
    path.trim();
    while (path.startsWith("/")) path.remove(0, 1);
    int slash = path.indexOf('/');
    share = slash < 0 ? path : path.substring(0, slash);
    prefix = slash < 0 ? String() : path.substring(slash + 1);
    return share.length() && share.length() <= 80 &&
           share != "." && share != ".." &&
           share.indexOf('\\') < 0 && share.indexOf(':') < 0 &&
           smbSafeRelative(prefix) &&
           (profile.port == 0 || profile.port == 445);
}

NasBrowseState NasService::browseState() const {
    NasBrowseState value{};
    if (browseLock_ && xSemaphoreTake(browseLock_, pdMS_TO_TICKS(15)) == pdTRUE) {
        value = browseState_;
        xSemaphoreGive(browseLock_);
    }
    return value;
}

bool NasService::browseEntry(uint8_t index, NasBrowseEntry& out) const {
    if (!browseLock_ || xSemaphoreTake(browseLock_, pdMS_TO_TICKS(15)) != pdTRUE)
        return false;
    bool valid = index < browseState_.count && !browseState_.busy;
    if (valid && browseEntries_) out = browseEntries_[index];
    else valid = false;
    xSemaphoreGive(browseLock_);
    return valid;
}

bool NasService::startSmbBrowse(size_t profileIndex, const String& folder) {
    if (!browseLock_ || !profiles_ || !browseEntries_ || profileIndex >= profileCount_ ||
        profiles_[profileIndex].protocol != NasProtocol::Smb2 ||
        !profiles_[profileIndex].enabled ||
        !NetworkService::instance().online() || !smbSafeRelative(folder)) return false;
    String share, prefix;
    if (!smbSplitShare(profiles_[profileIndex], share, prefix)) return false;
    if (xSemaphoreTake(browseLock_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    if (browseState_.busy) { xSemaphoreGive(browseLock_); return false; }
    browseState_.busy = true;
    browseState_.success = false;
    browseState_.count = 0;
    browseProfile_ = static_cast<uint8_t>(profileIndex);
    browseState_.profileId=profiles_[profileIndex].id;
    strlcpy(browseState_.message, "SMB-Verbindung wird aufgebaut ...", sizeof(browseState_.message));
    strlcpy(browseState_.folder, folder.c_str(), sizeof(browseState_.folder));
    xSemaphoreGive(browseLock_);

    NasJob* job = new (std::nothrow) NasJob();
    if (job) {
        job->owner = this;
        job->profile = profiles_[profileIndex];
        job->profileIndex = static_cast<uint8_t>(profileIndex);
        job->folder = folder;
    }
    if (!job || xTaskCreate(smbWorkerThunk, "hp-smb", 16384, job, 3, nullptr) != pdPASS) {
        delete job;
        finishSmbJob(false, "SMB-Worker konnte nicht starten.");
        return false;
    }
    return true;
}

bool NasService::startSmbDownload(uint8_t entryIndex,uint32_t expectedProfileId,const String& expectedFolder) {
    if (!browseLock_ || !StorageService::instance().sdAvailable() ||
        xSemaphoreTake(browseLock_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    const bool valid = browseEntries_ && profiles_ && !browseState_.busy && entryIndex < browseState_.count &&
                       !browseEntries_[entryIndex].directory &&
                       browseProfile_ < profileCount_ &&
                       profiles_[browseProfile_].protocol == NasProtocol::Smb2;
    if(valid&&!profiles_[browseProfile_].enabled){xSemaphoreGive(browseLock_);return false;}
    if(valid&&expectedProfileId&&(browseState_.profileId!=expectedProfileId||String(browseState_.folder)!=expectedFolder)){xSemaphoreGive(browseLock_);return false;}
    NasJob* job = nullptr;
    if (valid) {
        job = new (std::nothrow) NasJob();
        if (job) {
            job->owner = this;
            job->profileIndex = browseProfile_;
            job->profile = profiles_[browseProfile_];
            job->file = browseEntries_[entryIndex].path;
            browseState_.busy = true;
            browseState_.success = false;
            job->download = true;
            strlcpy(browseState_.message, "Datei wird auf SD kopiert ...", sizeof(browseState_.message));
        }
    }
    xSemaphoreGive(browseLock_);
    if (!job) return false;
    if (xTaskCreate(smbWorkerThunk, "hp-smb", 16384, job, 3, nullptr) != pdPASS) {
        delete job;
        finishSmbJob(false, "Download-Worker konnte nicht starten.");
        return false;
    }
    return true;
}

void NasService::finishSmbJob(bool ok, const String& message) {
    if (!browseLock_) return;
    xSemaphoreTake(browseLock_, portMAX_DELAY);
    browseState_.busy = false;
    browseState_.success = ok;
    ++browseState_.generation;
    strlcpy(browseState_.message, message.c_str(), sizeof(browseState_.message));
    xSemaphoreGive(browseLock_);
}

void NasService::smbWorkerThunk(void* arg) {
    NasJob* job = static_cast<NasJob*>(arg);
    job->owner->runSmbJob(*job);
    delete job;
    vTaskDelete(nullptr);
}

static smb2_context* connectProfile(const NasProfile& profile,String& prefix) {
    String share;
    if(!smbSplitShare(profile,share,prefix)||!NetworkService::instance().online()){NasService::instance().reportProvider(profile.id,false);return nullptr;}
    auto* ctx=smb2_init_context();if(!ctx)return nullptr;
    smb2_set_timeout(ctx,8);smb2_set_security_mode(ctx,SMB2_NEGOTIATE_SIGNING_ENABLED);
    String password=CredentialStore::instance().nasPassword(profile.id);
    smb2_set_password(ctx,password.c_str());
    for(size_t i=0;i<password.length();++i)password.setCharAt(i,'\0');
    if(smb2_connect_share(ctx,profile.host,share.c_str(),profile.username)!=0){smb2_destroy_context(ctx);NasService::instance().reportProvider(profile.id,false);return nullptr;}
    NasService::instance().reportProvider(profile.id,true);
    return ctx;
}
namespace {
struct ProviderSession {
    smb2_context* ctx=nullptr;String prefix;
    ProviderSession(NasService& service,uint32_t id){
        for(size_t i=0;i<service.profileCount();++i){NasProfile p;
            if(service.copyProfile(i,p)&&p.id==id&&p.enabled&&p.protocol==NasProtocol::Smb2){ctx=connectProfile(p,prefix);break;}}
    }
    ~ProviderSession(){if(ctx){smb2_disconnect_share(ctx);smb2_destroy_context(ctx);}}
    String path(const String& relative){return prefix.length()?prefix+(relative.length()?"/"+relative:""):relative;}
};
struct DirectoryReply {
    bool done=false,ok=false,end=false,stopped=false;uint8_t fileId[SMB2_FD_SIZE]{};
    NasService::Visitor visitor=nullptr;void* context=nullptr;
    static bool entry(const char* name,uint64_t size,uint64_t date,bool directory,void* data){auto& state=*static_cast<DirectoryReply*>(data);
        if(!strcmp(name,".")||!strcmp(name,".."))return true;
        return state.visitor(NasService::FileInfo{String(name),size,date,directory},state.context);
    }
};
bool waitDirectory(ProviderSession& session,DirectoryReply& state,const std::atomic<bool>* cancel){
    const uint32_t start=millis();while(!state.done){
        if((cancel&&cancel->load())||SecurityService::instance().tabletLocked()||uint32_t(millis()-start)>8000){
            // Destroy pending PDUs while their callback context is still alive.
            smb2_destroy_context(session.ctx);session.ctx=nullptr;return false;
        }
        pollfd fd{smb2_get_fd(session.ctx),static_cast<short>(smb2_which_events(session.ctx)),0};
        if(poll(&fd,1,50)<0||(fd.revents&&smb2_service(session.ctx,fd.revents)<0)){
            smb2_destroy_context(session.ctx);session.ctx=nullptr;return false;}
    }return state.ok;
}
}
bool NasService::visit(uint32_t id,const String& folder,Visitor visitor,void* context,const std::atomic<bool>* cancel){
    if(!visitor||!smbSafeRelative(folder)||(cancel&&cancel->load()))return false;
    ProviderSession session(*this,id);if(!session.ctx)return false;
    DirectoryReply state;state.visitor=visitor;state.context=context;const String remote=session.path(folder);
    smb2_create_request create{};create.requested_oplock_level=SMB2_OPLOCK_LEVEL_NONE;
    create.impersonation_level=SMB2_IMPERSONATION_IMPERSONATION;create.desired_access=SMB2_FILE_LIST_DIRECTORY|SMB2_FILE_READ_ATTRIBUTES;
    create.file_attributes=SMB2_FILE_ATTRIBUTE_DIRECTORY;create.share_access=SMB2_FILE_SHARE_READ|SMB2_FILE_SHARE_WRITE;
    create.create_disposition=SMB2_FILE_OPEN;create.create_options=SMB2_FILE_DIRECTORY_FILE;create.name=remote.c_str();
    auto* pdu=smb2_cmd_create_async(session.ctx,&create,[](smb2_context*,int status,void* reply,void* data){auto& s=*static_cast<DirectoryReply*>(data);
        s.ok=status==0&&reply;if(s.ok)memcpy(s.fileId,static_cast<smb2_create_reply*>(reply)->file_id,SMB2_FD_SIZE);s.done=true;},&state);
    if(!pdu)return false;smb2_queue_pdu(session.ctx,pdu);if(!waitDirectory(session,state,cancel))return false;
    bool ok=true;const uint32_t started=millis();
    while(!state.end&&!state.stopped){if(uint32_t(millis()-started)>15000){ok=false;break;}state.done=false;state.ok=false;
        smb2_query_directory_request query{};query.file_information_class=SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;
        memcpy(query.file_id,state.fileId,SMB2_FD_SIZE);query.output_buffer_length=8192;query.name="*";
        pdu=smb2_cmd_query_directory_async(session.ctx,&query,[](smb2_context*,int status,void* reply,void* data){auto& s=*static_cast<DirectoryReply*>(data);
            s.end=uint32_t(status)==SMB2_STATUS_NO_MORE_FILES;s.ok=s.end;
            if(status==0&&reply){auto& page=*static_cast<smb2_query_directory_reply*>(reply);s.ok=SmbDirectoryPage::visit(page.output_buffer,page.output_buffer_length,DirectoryReply::entry,&s,s.stopped);}
            s.done=true;},&state);
        if(!pdu){ok=false;break;}smb2_queue_pdu(session.ctx,pdu);if(!waitDirectory(session,state,cancel)){ok=false;break;}
    }
    // Disconnect closes the directory handle as well; no directory list retained.
    if(!ok)reportProvider(id,false);return ok;
}
bool NasService::statFile(uint32_t id,const String& path,FileInfo& info){
    if(!smbSafeRelative(path))return false;ProviderSession session(*this,id);if(!session.ctx)return false;
    smb2_stat_64 st{};if(smb2_stat(session.ctx,session.path(path).c_str(),&st)!=0)return false;
    info={path.substring(path.lastIndexOf('/')+1),st.smb2_size,st.smb2_mtime,st.smb2_type==SMB2_TYPE_DIRECTORY};return true;
}
bool NasService::readRange(uint32_t id,const String& path,uint64_t offset,uint8_t* bytes,size_t capacity,size_t& size,uint64_t& total,const std::atomic<bool>* cancel){
    size=0;total=0;if(!bytes||capacity>8192||!smbSafeRelative(path)||(cancel&&cancel->load())||SecurityService::instance().tabletLocked())return false;
    ProviderSession session(*this,id);if(!session.ctx)return false;
    smb2_stat_64 st{};const String remote=session.path(path);
    if(smb2_stat(session.ctx,remote.c_str(),&st)!=0||st.smb2_type!=SMB2_TYPE_FILE||offset>st.smb2_size)return false;
    total=st.smb2_size;auto* file=smb2_open(session.ctx,remote.c_str(),O_RDONLY);if(!file)return false;
    const size_t wanted=std::min<uint64_t>(capacity,total-offset);bool ok=true;
    while(size<wanted){if((cancel&&cancel->load())||SecurityService::instance().tabletLocked()){ok=false;break;}
        const int n=smb2_pread(session.ctx,file,bytes+size,wanted-size,offset+size);if(n<=0){ok=false;break;}size+=n;}
    smb2_close(session.ctx,file);return ok;
}
bool NasService::readFile(uint32_t id,const String& path,uint8_t* bytes,size_t capacity,size_t& size,const std::atomic<bool>* cancel){
    size=0;if(!bytes||!smbSafeRelative(path)||(cancel&&cancel->load()))return false;
    ProviderSession session(*this,id);if(!session.ctx)return false;
    smb2_stat_64 st{};const String remote=session.path(path);
    if(smb2_stat(session.ctx,remote.c_str(),&st)!=0||st.smb2_type!=SMB2_TYPE_FILE||st.smb2_size>capacity)return false;
    auto* file=smb2_open(session.ctx,remote.c_str(),O_RDONLY);if(!file)return false;
    bool ok=true;while(size<st.smb2_size){if(cancel&&cancel->load()){ok=false;break;}
        const int n=smb2_read(session.ctx,file,bytes+size,std::min<size_t>(8192,st.smb2_size-size));
        if(n<=0){ok=false;break;}size+=n;taskYIELD();}
    smb2_close(session.ctx,file);return ok;
}
void NasService::runSmbJob(NasJob& job) {
    PerformanceGuard boost;
    String share, prefix;
    if (!smbSplitShare(job.profile, share, prefix) ||
        !smbSafeRelative(job.folder) || !smbSafeRelative(job.file)) {
        finishSmbJob(false, "Ungueltiger Freigabepfad. Beispiel: Musik/Alben");
        return;
    }
    if (!NetworkService::instance().online()) {
        finishSmbJob(false, "LAN/WLAN ist nicht verbunden.");
        return;
    }
    struct smb2_context* ctx=connectProfile(job.profile,prefix);
    if(!ctx){finishSmbJob(false,"SMB-Verbindung nicht verfügbar.");return;}
    bool ok = false;
    String status;
    if (!job.download) {
        String relative = prefix;
        if (job.folder.length()) {
            if (relative.length()) relative += "/";
            relative += job.folder;
        }
        struct smb2dir* dir = smb2_opendir(ctx, relative.length() ? relative.c_str() : "");
        if (!dir) {
            status = String("Ordner konnte nicht geoeffnet werden: ") + smb2_get_error(ctx);
        } else {
            uint8_t count = 0;
            struct smb2dirent* entry = nullptr;
            while (count < kMaxBrowseEntries && (entry = smb2_readdir(ctx, dir))) {
                String name(entry->name ? entry->name : "");
                if (name.isEmpty() || name == "." || name == ".." ||
                    name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
                    name.length() >= sizeof(browseEntries_[0].name)) continue;
                String path = job.folder;
                if (path.length()) path += "/";
                path += name;
                if (!smbSafeRelative(path) || path.length() >= sizeof(browseEntries_[0].path)) continue;
                NasBrowseEntry target{};
                strlcpy(target.name, name.c_str(), sizeof(target.name));
                strlcpy(target.path, path.c_str(), sizeof(target.path));
                target.directory = entry->st.smb2_type == SMB2_TYPE_DIRECTORY;
                target.size = entry->st.smb2_size;
                xSemaphoreTake(browseLock_, portMAX_DELAY);
                browseEntries_[count] = target;
                ++count;
                xSemaphoreGive(browseLock_);
            }
            smb2_closedir(ctx, dir);
            xSemaphoreTake(browseLock_, portMAX_DELAY);
            browseState_.count = count;
            xSemaphoreGive(browseLock_);
            ok = true;
            status = "SMB verbunden: " + String(count) + " Eintraege (max. 80).";
        }
    } else {
        // Save SMB files on SD, never to the limited internal flash.
        const String remotePath = prefix.length() ? prefix + "/" + job.file : job.file;
        String filename = job.file.substring(job.file.lastIndexOf('/') + 1);
        if (!filename.length() || filename == "." || filename == ".." ||
            filename.indexOf('\\') >= 0 || filename.indexOf(':') >= 0 ||
            filename.length() > 80) {
            status = "Dateiname wird nicht unterstuetzt.";
        } else {
            String ext = filename; ext.toLowerCase();
            const bool isMusic = ext.endsWith(".mp3") || ext.endsWith(".wav");
            const String targetRel = String(isMusic ? "/music/" : "/nas/") + filename;
            auto& storage = StorageService::instance();
            auto* fs = storage.fsFor(StorageClass::Media);
            const String targetPath = storage.pathFor(StorageClass::Media, targetRel);
            const String tempPath = targetPath + ".part";
            struct smb2fh* remote = smb2_open(ctx, remotePath.c_str(), O_RDONLY);
            if (!remote) {
                status = String("Datei konnte nicht geoeffnet werden: ") + smb2_get_error(ctx);
            } else if (fs && fs->exists(targetPath)) {
                status = "Die Datei existiert bereits auf SD (nichts ueberschrieben).";
                smb2_close(ctx, remote);
            } else if (!fs || !storage.ensureParentDirs(StorageClass::Media, targetRel)) {
                status = "SD-Karte oder Zielordner nicht verfuegbar.";
                smb2_close(ctx, remote);
            } else {
                fs->remove(tempPath);
                File local = fs->open(tempPath, FILE_WRITE);
                if (!local) status = "SD-Datei konnte nicht angelegt werden.";
                else {
                    uint8_t buffer[8192];
                    uint64_t bytes = 0;
                    ok = true;
                    for (;;) {
                        int n = smb2_read(ctx, remote, buffer, sizeof(buffer));
                        if (n < 0) { ok = false; status = "SMB-Lesefehler."; break; }
                        if (n == 0) break;
                        if (local.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
                            ok = false; status = "SD-Schreibfehler oder SD voll."; break;
                        }
                        bytes += static_cast<uint32_t>(n);
                        vTaskDelay(1);
                    }
                    local.flush(); local.close();
                    if (ok) {
                        // Do not overwrite any user's local file.
                        if (fs->exists(targetPath) || !fs->rename(tempPath, targetPath)) {
                            ok = false;
                            status = "Dateiname vorhanden oder Umbenennen fehlgeschlagen.";
                        } else {
                            status = "Auf SD gespeichert (" + String(static_cast<unsigned long>(bytes / 1024)) + " KiB): " + filename;
                            // The existing PCM/audio service is NOT a working SMB audio decoder.
                            // Downloaded MP3/WAV will be found by the existing music library after rescan.
                            if (isMusic) {
                                status += " (Musikbibliothek wird automatisch aktualisiert)";
                                EventBus::instance().publish({EventType::MediaLibraryChanged, nullptr});
                            }
                        }
                    }
                    if (!ok) fs->remove(tempPath);
                }
                smb2_close(ctx, remote);
            }
        }
    }
    smb2_disconnect_share(ctx);
    smb2_destroy_context(ctx);
    finishSmbJob(ok, status);
}


}
