#include "PhotoLibraryService.h"
#include "NetworkWorker.h"
#include "ConfigService.h"
#include "SecurityService.h"
#include "TimeService.h"
#include "IdleService.h"
#include "ImageRenderPolicy.h"
#include "../core/AppManager.h"
#include "../ui/UiShell.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <new>
#include <algorithm>
#include <strings.h>
namespace hp {namespace {
struct SettingsJob {std::shared_ptr<PhotoSettingsRequest> result;};
void settingsWorker(void* pointer){std::unique_ptr<SettingsJob> job(static_cast<SettingsJob*>(pointer));auto r=job->result;
    if(r->load){SecurityWriteGuard guard;r->success=PhotoLibraryService::readStored(r->settings,r->message);r->expectedRevision=ConfigService::instance().revision();}
    else{SecurityWriteGuard guard;if(r->expectedRevision!=ConfigService::instance().revision())r->message="revision_conflict";
        else r->success=PhotoLibraryService::writeStored(r->settings,r->message);if(r->success)r->expectedRevision=ConfigService::instance().revision();}r->ready=true;
}
struct ScanJob {
    std::shared_ptr<PhotoScanRequest> result;PhotoSettings settings;bool favorites=false;
    char folders[64][PhotoLibrary::pathSize]{};uint8_t depths[64]{};bool recursive[64]{};size_t count=0,at=0;String current;
};
void addImage(ScanJob& job,const String& uri,uint64_t modified=0){auto& library=*job.result->library;
    if(uri.length()>=PhotoLibrary::pathSize){library.truncated=true;return;}
    for(size_t i=0;i<library.count;++i)if(uri==library.paths[i])return;
    if(library.count==PhotoLibrary::limit){library.truncated=true;return;}
    library.order[library.count]=library.count;library.modified[library.count]=modified;strlcpy(library.paths[library.count++],uri.c_str(),PhotoLibrary::pathSize);
}
bool visitPhoto(const NasService::FileInfo& info,void* context){auto& job=*static_cast<ScanJob*>(context);
    if(job.result->canceled||SecurityService::instance().tabletLocked())return false;
    if(!info.name.length()||info.name=="."||info.name==".."||info.name.indexOf('/')>=0||info.name.indexOf('\\')>=0)return true;
    const String uri=job.current+(job.current.endsWith("/")?"":"/")+info.name;
    if(!PhotoLibraryService::validPhotoUri(uri))return true;
    if(info.directory&&job.recursive[job.at]){
        if(job.count>=64||job.depths[job.at]>=4){job.result->library->truncated=true;return true;}
        for(size_t i=0;i<job.count;++i)if(uri==job.folders[i]){job.recursive[i]=true;return true;}
        strlcpy(job.folders[job.count],uri.c_str(),PhotoLibrary::pathSize);job.recursive[job.count]=true;job.depths[job.count++]=job.depths[job.at]+1;
    }else if(!info.directory&&ImageCodec::supported(info.name.c_str()))addImage(job,uri,info.modified);
    taskYIELD();return job.result->library->count<PhotoLibrary::limit;
}
void deleteScan(ScanJob* job){if(job){job->~ScanJob();heap_caps_free(job);}}
void scanWorker(void* pointer){std::unique_ptr<ScanJob,decltype(&deleteScan)> job(static_cast<ScanJob*>(pointer),deleteScan);auto r=job->result;
    if(r->canceled){r->ready=true;return;}
    void* memory=heap_caps_malloc(sizeof(PhotoLibrary),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory){r->message="Nicht genug PSRAM für die Fotoliste.";r->ready=true;return;}
    r->library=std::shared_ptr<PhotoLibrary>(new(memory) PhotoLibrary,[](PhotoLibrary* p){p->~PhotoLibrary();heap_caps_free(p);});
    if(job->favorites){for(const auto& f:job->settings.favorites)if(f.length()&&ImageCodec::supported(f.c_str())){NasService::FileInfo info;
        if((!f.startsWith("nas")||ConfigService::instance().getBool("files_nas",false))&&FileBrowserService::stat(f,info)&&!info.directory)addImage(*job,f,info.modified);}}
    else {
        // Missing/unmounted default media are normal, not scan errors. Probe
        // only in this existing worker; never create storage from a UI callback.
        if(job->settings.automaticFolders)for(const char* root:{"sd:/Bilder","usb:/Bilder"}){
            if(r->canceled)break;NasService::FileInfo info;
            if(FileBrowserService::stat(root,info)&&info.directory){strlcpy(job->folders[job->count],root,PhotoLibrary::pathSize);job->recursive[job->count++]=true;}}
        for(const auto& f:job->settings.folders)if(f.length()){
            bool found=false;for(size_t i=0;i<job->count;++i)if(f==job->folders[i]){job->recursive[i]|=job->settings.recursive;found=true;break;}
            if(!found){strlcpy(job->folders[job->count],f.c_str(),PhotoLibrary::pathSize);job->recursive[job->count++]=job->settings.recursive;}}
        size_t errors=0;for(job->at=0;job->at<job->count&&!r->canceled;++job->at){job->current=job->folders[job->at];
            if(job->current.startsWith("nas")&&!ConfigService::instance().getBool("files_nas",false)){++errors;continue;}
            if(!FileBrowserService::visit(job->current,visitPhoto,job.get(),&r->canceled))++errors;
            if(r->library->count==PhotoLibrary::limit){r->library->truncated=true;break;}}
        if(errors)r->message=String(static_cast<unsigned>(errors))+" Ordner nicht erreichbar. Letztes Bild bleibt bei Lesefehlern erhalten.";
    }
    auto& library=*r->library;std::sort(library.order,library.order+library.count,[&](uint16_t a,uint16_t b){
        if(job->settings.sortDate&&library.modified[a]!=library.modified[b]){
            if(!library.modified[a]||!library.modified[b])return library.modified[a]!=0;
            return job->settings.descending?library.modified[a]>library.modified[b]:library.modified[a]<library.modified[b];}
        const char* one=strrchr(library.paths[a],'/'),*two=strrchr(library.paths[b],'/');const int name=strcasecmp(one?one+1:library.paths[a],two?two+1:library.paths[b]);
        if(name)return !job->settings.sortDate&&job->settings.descending?name>0:name<0;return strcmp(library.paths[a],library.paths[b])<0;});
    if(r->library->truncated)r->message="Bibliothekslimit: 1024 Bilder / 64 Ordner / 4 Unterordnerebenen. Auswahl eingrenzen.";
    if(!r->library->count&&!r->message.length())r->message="Keine unterstützten Bilder gefunden. Ordner auswählen.";
    r->ready=true;
}
}
PhotoLibraryService& PhotoLibraryService::instance(){static PhotoLibraryService service;return service;}
std::shared_ptr<PhotoDeleteRequest> PhotoLibraryService::removePhoto(const String& uri,DirectoryPageCache::Revision revision){
    auto request=std::make_shared<PhotoDeleteRequest>();request->uri=uri;request->revision=revision;
    auto* job=new(std::nothrow) std::shared_ptr<PhotoDeleteRequest>(request);
    if(!job||!NetworkWorker::instance().submitLocalTransfer([](void* pointer){std::unique_ptr<std::shared_ptr<PhotoDeleteRequest>> holder(static_cast<std::shared_ptr<PhotoDeleteRequest>*>(pointer));auto r=*holder;
        r->success=FileBrowserService::removePhoto(r->uri,r->revision,&r->canceled,r->message);
        if(r->success){SecurityWriteGuard guard;PhotoSettings settings;String error;if(readStored(settings,error)){bool changed=false;for(auto& f:settings.favorites)if(f==r->uri){f="";changed=true;}if(changed&&!writeStored(settings,error))r->message="Bild gelöscht; Favorit konnte nicht entfernt werden. Einstellungen prüfen.";}}
        r->ready=true;
    },job,"photo-delete")){delete job;request->message="Dateidienst beschäftigt. Erneut versuchen.";request->ready=true;}
    return request;
}
String PhotoLibraryService::settingsJson(const PhotoSettings& s){JsonDocument d;d["version"]=1;d["start"]=s.start;d["end"]=s.end;d["interval"]=s.interval;d["days"]=s.days;d["schedule"]=s.schedule;d["recursive"]=s.recursive;d["automaticFolders"]=s.automaticFolders;
    d["takeover"]=s.takeover;d["returnAfter"]=s.returnAfter;d["sortDate"]=s.sortDate;d["descending"]=s.descending;d["renderEdge"]=s.renderEdge;
    auto folders=d["folders"].to<JsonArray>();for(const auto& f:s.folders)if(f.length())folders.add(f);
    auto favorites=d["favorites"].to<JsonArray>();for(const auto& f:s.favorites)if(f.length())favorites.add(f);String raw;serializeJson(d,raw);return raw;
}
bool PhotoLibraryService::settingsFromJson(const String& raw,PhotoSettings& s,String& error){JsonDocument d;
    if(raw.length()>12288||deserializeJson(d,raw,DeserializationOption::NestingLimit(4))||d["version"]!=1||!d["folders"].is<JsonArray>()||!d["favorites"].is<JsonArray>()||d["folders"].size()>8||d["favorites"].size()>32){error="photos_settings_invalid";return false;}
    for(const char* key:{"start","end","interval","returnAfter","days","renderEdge"})if(!d[key].isNull()&&!d[key].is<unsigned>()){error="photos_settings_type";return false;}
    for(const char* key:{"schedule","recursive","automaticFolders","takeover","sortDate","descending"})if(!d[key].isNull()&&!d[key].is<bool>()){error="photos_settings_type";return false;}
    // Validate wide values before narrowing: e.g. 65536 must not wrap to 0.
    if((d["start"]|420u)>=1440||(d["end"]|1140u)>=1440||(d["days"]|127u)>127||(d["interval"]|30u)>3600||(d["returnAfter"]|30u)>3600||!ImageRenderPolicy::quality(d["renderEdge"]|1280u)){error="photos_settings_range";return false;}
    s={};s.start=d["start"]|420;s.end=d["end"]|1140;s.interval=d["interval"]|30;s.days=d["days"]|127;s.schedule=d["schedule"]|false;s.recursive=d["recursive"]|false;s.automaticFolders=d["automaticFolders"]|true;
    s.takeover=d["takeover"]|true;s.returnAfter=d["returnAfter"]|30;s.sortDate=d["sortDate"]|true;s.descending=d["descending"]|true;s.renderEdge=d["renderEdge"]|1280;
    size_t i=0;for(auto v:d["folders"].as<JsonArrayConst>()){if(!v.is<const char*>()){error="photos_folder_type";return false;}s.folders[i++]=v.as<String>();}
    i=0;for(auto v:d["favorites"].as<JsonArrayConst>()){if(!v.is<const char*>()){error="photos_favorite_type";return false;}s.favorites[i++]=v.as<String>();}
    if(!validSettings(s)){error="photos_settings_invalid";return false;}return true;
}
bool PhotoLibraryService::readStored(PhotoSettings& s,String& error){const String raw=ConfigService::instance().getString("photos_cfg","");if(!raw.length()){s={};return true;}return settingsFromJson(raw,s,error);}
bool PhotoLibraryService::writeStored(const PhotoSettings& s,String& error){SecurityWriteGuard guard;
    if(!validSettings(s)||SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){error="settings_locked_or_invalid";return false;}
    const String raw=settingsJson(s);if(raw.length()>12288||!ConfigService::instance().putString("photos_cfg",raw)){error="photos_write_failed";return false;}error="Gespeichert.";return true;
}
bool PhotoLibraryService::validPhotoUri(const String& uri){return uri.length()==strlen(uri.c_str())&&uri.length()<PhotoLibrary::pathSize&&
    (uri.startsWith("sd:/")||uri.startsWith("usb:/")||uri.startsWith("nas")||uri.startsWith("data:/"))&&FileBrowserService::validUri(uri);}
bool PhotoLibraryService::validSettings(const PhotoSettings& s){
    if(s.start>=1440||s.end>=1440||s.interval<5||s.interval>3600||s.returnAfter<5||s.returnAfter>3600||s.days>127||(s.schedule&&!s.days)||!ImageRenderPolicy::quality(s.renderEdge))return false;
    for(const auto& f:s.folders)if(f.length()&&!validPhotoUri(f))return false;
    for(const auto& f:s.favorites)if(f.length()&&(!validPhotoUri(f)||!ImageCodec::supported(f.c_str())))return false;
    return true;
}
bool PhotoLibraryService::begin(bool force){if(pending_||(loaded_&&!force))return true;pending_=std::make_shared<PhotoSettingsRequest>();pending_->load=true;
    auto* job=new(std::nothrow) SettingsJob{pending_};if(!job||!NetworkWorker::instance().submitLocalTransfer(settingsWorker,job,"photos-config")){delete job;pending_.reset();return false;}return true;}
bool PhotoLibraryService::save(const PhotoSettings& settings){
    if(pending_||!loaded_||!validSettings(settings)||SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed())return false;
    pending_=std::make_shared<PhotoSettingsRequest>();pending_->settings=settings;pending_->expectedRevision=configRevision_;auto* job=new(std::nothrow) SettingsJob{pending_};
    if(!job||!NetworkWorker::instance().submitLocalTransfer(settingsWorker,job,"photos-save")){delete job;pending_.reset();return false;}return true;
}
bool PhotoLibraryService::favorite(const String& uri)const{for(const auto& f:settings_.favorites)if(f==uri)return true;return false;}
bool PhotoLibraryService::toggleFavorite(const String& uri){if(!validPhotoUri(uri)||!ImageCodec::supported(uri.c_str()))return false;
    PhotoSettings next=settings_;for(auto& f:next.favorites)if(f==uri){f="";return save(next);}
    for(auto& f:next.favorites)if(!f.length()){f=uri;return save(next);}status_="Maximal 32 Favoriten.";return false;
}
std::shared_ptr<PhotoScanRequest> PhotoLibraryService::scan(const PhotoSettings& settings,bool favorites){auto r=std::make_shared<PhotoScanRequest>();
    void* memory=heap_caps_malloc(sizeof(ScanJob),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);auto* job=memory?new(memory) ScanJob:nullptr;
    if(job){job->result=r;job->settings=settings;job->favorites=favorites;}
    if(!job||!validSettings(settings)||!NetworkWorker::instance().submitTransfer(scanWorker,job,"photos-scan")){deleteScan(job);r->message="Fotodienst beschäftigt / Auswahl ungültig.";r->ready=true;}return r;
}
bool PhotoLibraryService::startFrame(bool automatic){if(SecurityService::instance().tabletLocked())return false;
    bool folders=settings_.automaticFolders;for(const auto& f:settings_.folders)folders|=f.length()>0;if(!loaded_||!folders){status_="Zuerst mindestens einen Foto-Quellordner auswählen.";return false;}
    if(automatic&&settings_.schedule){struct tm local{};auto& time=TimeService::instance();if(!time.localNow(local)||!photoWindow(time.valid(),local.tm_wday,local.tm_hour*60+local.tm_min,settings_.days,settings_.start,settings_.end))return false;}
    frame_=true;automatic_=automatic;return true;
}
void PhotoLibraryService::stopFrame(){frame_=false;automatic_=false;IdleService::instance().keepAwake(IdleLease::PhotoFrame,0);}
void PhotoLibraryService::frameFailed(){stopFrame();retryAt_=millis()+300000;}
void PhotoLibraryService::pollUi(bool interrupted){
    const uint32_t now=millis();if(lastPoll_&&uint32_t(now-lastPoll_)<1000)return;lastPoll_=now;
    if(mediaDirty_.exchange(false))++revision_;
    if(pending_&&pending_->ready){status_=pending_->message;if(pending_->success){configRevision_=pending_->expectedRevision;settings_=pending_->settings;loaded_=true;++revision_;}else if(pending_->load){configRevision_=pending_->expectedRevision;settings_={};loaded_=true;}pending_.reset();}
    if(loaded_&&!pending_&&configRevision_!=ConfigService::instance().revision())begin(true);
    if(!loaded_&&!pending_)begin();
    const auto control=control_.exchange(0);if(control==1){pausedWindow_=true;stopFrame();}else if(control==2){pausedWindow_=false;retryAt_=0;previousWindow_=false;}
    if(control==3){if(!interrupted&&!SecurityService::instance().tabletLocked())UiShell::instance().requestAppOpen("photos","{\"frame\":true,\"manual\":true}");return;}
    auto* app=AppManager::instance().current();const bool photos=app&&!strcmp(app->id(),"photos");
    if(frame_&&(!photos||interrupted||SecurityService::instance().tabletLocked()))stopFrame();
    if(frame_&&automatic_&&!settings_.schedule){stopFrame();if(photos)UiShell::instance().showHome();return;}
    struct tm local{};auto& time=TimeService::instance();const bool window=loaded_&&settings_.schedule&&time.localNow(local)&&photoWindow(time.valid(),local.tm_wday,local.tm_hour*60+local.tm_min,settings_.days,settings_.start,settings_.end);
    const unsigned minute=local.tm_hour*60+local.tm_min;int year=local.tm_year+1900,day=local.tm_yday;
    if(settings_.start>settings_.end&&minute<settings_.end&&--day<0){--year;day=(year%4==0&&(year%100!=0||year%400==0))?365:364;}
    const int key=year*400+day;
    const bool newWindow=window&&(!previousWindow_||key!=windowKey_);if(!window||newWindow)pausedWindow_=false;
    previousWindow_=window;windowKey_=key;
    if(control==1)pausedWindow_=true;
    if(frame_&&automatic_&&!window){stopFrame();if(photos)UiShell::instance().showHome();return;}
    if(frame_&&photos){IdleService::instance().keepAwake(IdleLease::PhotoFrame,5000);return;}
    bool folders=settings_.automaticFolders;for(const auto& f:settings_.folders)folders|=f.length()>0;
    if(window&&!pausedWindow_&&folders&&(settings_.takeover||!app)&&!interrupted&&!SecurityService::instance().tabletLocked()&&(!retryAt_||int32_t(now-retryAt_)>=0)){
        IdleService::instance().keepAwake(IdleLease::PhotoFrame,5000);
        auto idle=IdleService::instance().profile();unsigned seconds=settings_.returnAfter;
        for(unsigned threshold:{unsigned(idle.dimAfter),unsigned(idle.saverEnabled?idle.saverAfter:0),unsigned(idle.offAfter)})if(threshold&&threshold<seconds)seconds=threshold;
        if((newWindow&&settings_.takeover)||uint32_t(now-IdleService::instance().lastActivityMillis())>=seconds*1000u){
            if(UiShell::instance().requestAppOpen("photos","{\"frame\":true}"))automatic_=true;}
    }else IdleService::instance().keepAwake(IdleLease::PhotoFrame,0);
}
}
