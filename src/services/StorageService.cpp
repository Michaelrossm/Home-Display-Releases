#include "StorageService.h"
#include "HomePanelConfig.h"
#include "BoardConfig.h"
#include "../core/EventBus.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <unistd.h>
#include <utime.h>
#include "ConfigService.h"
#include "SecurityService.h"
#include "UsbStorageService.h"
#include "UsbAppFileSystem.h"
#include "ExternalStorageLayout.h"
#include "NetworkWorker.h"
#include <esp_system.h>
#include <ff.h>
#include <diskio.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <new>
namespace hp {
std::mutex& StorageService::mutationMutex(){static std::mutex mutex;return mutex;}
StorageService& StorageService::instance(){static StorageService s;return s;}
bool StorageService::archiveVolume(ArchiveVolume& v){
 v=ArchiveVolume{};
 if(sdReady_){v.fs=&SD_MMC;return true;}
 auto& usb=UsbStorageService::instance();if(!usb.available())return false;
 v.usb=true;v.generation=usb.generation();v.owned=usbAppFileSystemFor(v.generation);v.fs=v.owned.get();
 return v.fs&&archiveVolumeValid(v);
}
bool StorageService::archiveVolumeValid(const ArchiveVolume& v)const{return v.fs&&(v.usb?UsbStorageService::instance().validFiles(v.generation):sdReady_);}
bool StorageService::archiveParents(ArchiveVolume& v,const String& relative){
 if(!validateRelativePath(relative)||!archiveVolumeValid(v))return false;
 const String path=String("/homepanel/data/")+relative;
 for(int pos=1;(pos=path.indexOf('/',pos))>=0;++pos){const String parent=path.substring(0,pos);
  if(!archiveVolumeValid(v)||(!v.fs->exists(parent)&&!v.fs->mkdir(parent)))return false;
 }return true;
}
bool StorageService::archiveTruncate(ArchiveVolume& v,const String& relative,size_t size){
 if(!validateRelativePath(relative)||!archiveVolumeValid(v))return false;
 if(!v.usb)return truncateSdUserData(relative,size);
 auto& usb=UsbStorageService::instance();if(!usb.acquireFiles())return false;
 const bool ok=archiveVolumeValid(v)&&::truncate((String(UsbStorageService::mountPath())+"/homepanel/data/"+relative).c_str(),size)==0;
 usb.releaseFiles();return ok;
}
uint64_t StorageService::archiveFreeBytes(const ArchiveVolume& v){
 if(!archiveVolumeValid(v))return 0;if(!v.usb)return sdFreeBytes();
 uint64_t total=0,used=0;return UsbStorageService::instance().usage(total,used)&&total>used?total-used:0;
}
StorageClass StorageService::newAppStorage()const{
 const String value=ConfigService::instance().getString("app_store","");
 if(value=="usb")return StorageClass::UsbAppPackage;
 if(value=="sd")return StorageClass::Media;
 if(value=="internal")return StorageClass::AppPackage;
 return ConfigService::instance().getBool("app_store_sd",false)?StorageClass::Media:StorageClass::AppPackage;
}
bool StorageService::setNewAppStorage(StorageClass c){
 if(c!=StorageClass::AppPackage&&c!=StorageClass::Media&&c!=StorageClass::UsbAppPackage)return false;
 return ConfigService::instance().putString("app_store",c==StorageClass::UsbAppPackage?"usb":c==StorageClass::Media?"sd":"internal");
}
namespace {
String usageSize(uint64_t bytes) {
    if (bytes < 1024) return String(static_cast<unsigned long>(bytes)) + " B";
    if (bytes < 1024ULL * 1024) return String(double(bytes) / 1024, 1) + " KiB";
    if (bytes < 1024ULL * 1024 * 1024) return String(double(bytes) / (1024 * 1024), 2) + " MiB";
    return String(double(bytes) / (1024ULL * 1024 * 1024), 2) + " GiB";
}
String usageVolume(const char* title, uint64_t total, uint64_t used) {
    return String(title) + "\nBelegt: " + usageSize(used) + " | Frei: " +
        usageSize(total > used ? total - used : 0) + " | Gesamt: " + usageSize(total) + "\n";
}
// Bounded scans: one refresh request, never repeated on every UI tick.
uint64_t usageTree(fs::FS& fs, const String& path, const char* medium,
                   String& apps, size_t& budget, bool& partial, unsigned depth = 0,
                   StorageUsageSnapshot* snapshot = nullptr) {
    if (!budget || depth > 8) { partial = true; return 0; }
    --budget;
    File node = fs.open(path, FILE_READ);
    if (!node) return 0;
    if (!node.isDirectory()) { const uint64_t size = node.size(); node.close(); return size; }
    const bool package = fs.exists(path + "/main.lua") &&
        (fs.exists(path + "/app.json") || fs.exists(path + "/manifest.json"));
    uint64_t bytes = 0;
    File child = node.openNextFile();
    while (child) {
        const String childPath = child.path(); child.close();
        bytes += usageTree(fs, childPath, medium, apps, budget, partial, depth + 1, snapshot);
        if (!budget) { partial = true; break; }
        delay(0);
        child = node.openNextFile();
    }
    node.close();
    if (package && apps.length() < 6000 && !path.startsWith("/installer_")) {
        apps += path.substring(path.lastIndexOf('/') + 1) + " [" + medium + "]: " + usageSize(bytes) + "\n";
        if(snapshot && snapshot->appCount < 32){
            auto& app=snapshot->apps[snapshot->appCount++];
            snprintf(app.id,sizeof(app.id),"%s",path.substring(path.lastIndexOf('/')+1).c_str());
            app.bytes=bytes;app.sd=!strcmp(medium,"SD");app.usb=!strcmp(medium,"USB");
        }else if(snapshot)partial=true;
    }
    return bytes;
}
}

bool StorageService::requestUsageReport() {
    if (!usageMutex_) usageMutex_ = xSemaphoreCreateMutex();
    if (!usageMutex_) return false;
    bool idle = false;
    if (!usagePending_.compare_exchange_strong(idle, true)) return true;
    if (NetworkWorker::instance().submit(collectUsage, this, "storage-usage")) return true;
    usagePending_ = false;
    return false;
}
String StorageService::usageReport() {
    if (!usageMutex_ || xSemaphoreTake(usageMutex_, 0) != pdTRUE) return "Speicheranalyse läuft ...";
    String report = usageReport_.length() ? usageReport_ : String("Speicheranalyse läuft ...");
    xSemaphoreGive(usageMutex_);
    return report;
}
bool StorageService::usageSnapshot(StorageUsageSnapshot& out){
    if(!usageMutex_||xSemaphoreTake(usageMutex_,0)!=pdTRUE)return false;
    const bool ready=usageSnapshot_.generation!=0;
    if(ready)out=usageSnapshot_;
    xSemaphoreGive(usageMutex_);return ready;
}
void StorageService::collectUsage(void* context) {
    auto& self = *static_cast<StorageService*>(context);
    std::unique_ptr<StorageUsageSnapshot> data(new(std::nothrow) StorageUsageSnapshot{});
    if(!data){self.usagePending_=false;return;}
    String report, apps;
    const uint64_t flash = ESP.getFlashChipSize();
    data->flashTotal=flash;
    const auto* running = esp_ota_get_running_partition();
    report = "Interner Flash: " + usageSize(flash) + "\nFirmware (inkl. nativer Apps): " + usageSize(ESP.getSketchSize());
    if (running) report += "\nAktiver OTA-Bereich: " + usageSize(running->size);
    if(running)data->volumes[0]={running->size,ESP.getSketchSize(),true};
    const auto* next = esp_ota_get_next_update_partition(nullptr);
    if (next) report += "\nReservierter OTA-/Rollback-Bereich: " + usageSize(next->size);
    if(next)data->otaReserve=next->size;
    report += "\nOTA-Reserve ist kein freier App-Speicher.\n\n";
    size_t budget = 4096; bool partial = false;
    if (self.appFsReady_) {
        data->volumes[1]={self.appFs_.totalBytes(),self.appFs_.usedBytes(),true};
        report += usageVolume("Interne App-Partition", self.appFs_.totalBytes(), self.appFs_.usedBytes());
        usageTree(self.appFs_, "/", "Intern", apps, budget, partial, 0, data.get());
    } else report += "Interne App-Partition: nicht verfügbar\n";
    if (self.internalReady_) {
        data->volumes[2]={LittleFS.totalBytes(),LittleFS.usedBytes(),true};
        report += "\n" + usageVolume("Systemdaten-Dateisystem", LittleFS.totalBytes(), LittleFS.usedBytes());
        String ignored;
        size_t categoryIndex=0;
        for (const char* path : {"/config", "/cache", "/systemdata", "/recovery"}) {
            const auto bytes=usageTree(LittleFS,path,"",ignored,budget,partial);
            data->categories[categoryIndex++]=bytes;
            report += String(path) + ": " + usageSize(bytes) + "\n";
        }
    } else report += "\nSystemdaten-Dateisystem: nicht verfügbar\n";
    if (self.sdReady_) {
        data->volumes[3]={SD_MMC.totalBytes(),SD_MMC.usedBytes(),true};
        report += "\n" + usageVolume("SD-Karte", SD_MMC.totalBytes(), SD_MMC.usedBytes());
        const uint64_t sdApps = usageTree(SD_MMC, "/Apps", "SD", apps, budget, partial, 0, data.get());
        report += "App-Verzeichnis: " + usageSize(sdApps) + "\n";
        String ignored;
        size_t categoryIndex=4;
        for (const char* category : {"data", "media", "logs", "voice"}) {
            const auto bytes=usageTree(SD_MMC,String(kSdRoot)+"/"+category,"",ignored,budget,partial);
            data->categories[categoryIndex++]=bytes;
            report += String(category) + ": " + usageSize(bytes) + "\n";
        }
    } else report += "\nSD-Karte: nicht vorhanden\n";
    uint64_t usbTotal=0,usbUsed=0;
    if(UsbStorageService::instance().usage(usbTotal,usbUsed)){
        data->volumes[6]={usbTotal,usbUsed,true};
        report += "\n"+usageVolume("USB-Speicher",usbTotal,usbUsed);
        const auto generation=UsbStorageService::instance().generation();
        if(auto fs=usbAppFileSystemFor(generation))usageTree(*fs,"/Apps","USB",apps,budget,partial,0,data.get());
    }else report += String("\nUSB-Speicher: ")+UsbStorageService::instance().formatStatus()+"\n";
    report += "\nApp-Pakete (Dateigrößen, ohne Dateisystem-Overhead)\n";
    report += apps.length() ? apps : String("Keine installierten Lua-Pakete gefunden.\n");
    if (partial) report += "Analyse begrenzt; Dateigrößen möglicherweise unvollständig.\n";
    report += "Native Apps sind Teil der Firmware und nicht separat löschbar.\nBelegung ist eine Momentaufnahme; freier Flash ist je Partition gebunden.\n\n";
    report += usageVolume("Interner RAM (flüchtig)", heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    report += usageVolume("PSRAM (flüchtig, kein Installationsspeicher)", heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (xSemaphoreTake(self.usageMutex_, portMAX_DELAY) == pdTRUE) {
        for(size_t i=4;i<6;++i){const uint32_t caps=i==4?(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT):MALLOC_CAP_SPIRAM;
            const uint64_t total=heap_caps_get_total_size(caps),free=heap_caps_get_free_size(caps);
            data->volumes[i]={total,total>free?total-free:0,total!=0};}
        data->incomplete=partial;data->capturedMs=millis();data->generation=self.usageSnapshot_.generation+1;
        if(!data->generation)data->generation=1;
        self.usageSnapshot_=*data;self.usageReport_ = report; xSemaphoreGive(self.usageMutex_);
    }
    self.usagePending_ = false;
}
namespace {
// Resolve the FAT drive by checking geometry AND raw contents against the actual
// mounted SD. Never guess "0:"; ambiguous matches fail before any write.
bool sdFormatTarget(String& drive,String& fingerprint,FATFS*& mounted){
    if(SD_MMC.cardType()==CARD_NONE||SD_MMC.sectorSize()!=512)return false;
    uint8_t sector[512],candidate[512],next[512];if(!SD_MMC.readRAW(sector,0)||!SD_MMC.readRAW(next,1))return false;
    unsigned matches=0;uint8_t volumeBoot[512];
    for(unsigned i=0;i<FF_VOLUMES;++i){char name[4];snprintf(name,sizeof(name),"%u:",i);DWORD freeClusters=0;FATFS* filesystem=nullptr;LBA_t sectors=0;
        if(f_getfree(name,&freeClusters,&filesystem)!=FR_OK||disk_ioctl(i,GET_SECTOR_COUNT,&sectors)!=RES_OK||sectors!=static_cast<LBA_t>(SD_MMC.numSectors())||disk_read(i,candidate,0,1)!=RES_OK||memcmp(sector,candidate,512))continue;
        if(disk_read(i,candidate,1,1)!=RES_OK||memcmp(next,candidate,512))continue;
        if(disk_read(i,volumeBoot,filesystem->volbase,1)!=RES_OK||volumeBoot[510]!=0x55||volumeBoot[511]!=0xaa||volumeBoot[11]!=0||volumeBoot[12]!=2)return false;
        ++matches;drive=name;mounted=filesystem;
    }
    if(matches!=1)return false;
    uint8_t image[1032],hash[32];memcpy(image,sector,512);memcpy(image+512,volumeBoot,512);const uint64_t size=SD_MMC.cardSize();memcpy(image+1024,&size,8);
    if(mbedtls_sha256(image,sizeof(image),hash,0)!=0)return false;char text[65];for(unsigned i=0;i<32;++i)snprintf(text+i*2,3,"%02x",hash[i]);fingerprint=text;return true;
}
}
const char* StorageService::sdFormatStatus()const{switch(sdFormatState_.load()){case 1:return "Auftrag wird geprueft; danach Neustart ...";case 2:return "SD-Karte wurde formatiert.";case 3:return "Formatierung abgebrochen/fehlgeschlagen. Kein automatischer Wiederholungsversuch.";default:return "";}}
bool StorageService::requestSdFormat(){if(!sdReady_||!SecurityService::instance().settingsAllowed())return false;uint8_t expected=sdFormatState_.load();if(expected==1||!sdFormatState_.compare_exchange_strong(expected,1))return false;if(NetworkWorker::instance().submit(prepareSdFormat,this,"sd-format"))return true;sdFormatState_=3;return false;}
void StorageService::prepareSdFormat(void* context){auto& self=*static_cast<StorageService*>(context);SecurityWriteGuard guard;String drive,token;FATFS* filesystem=nullptr;
    auto& config=ConfigService::instance();
    if(!self.sdReady_||!SecurityService::instance().settingsAllowed()||!sdFormatTarget(drive,token,filesystem)||!config.putString("sd_fmt_req",String(config.getU32("boot_count")+1)+":"+token)){self.sdFormatState_=3;return;}
    // No live unmount: all clients disappear on the explicitly confirmed reboot.
    esp_restart();
}
void StorageService::applyBootSdFormat(bool bootHealthy){auto& config=ConfigService::instance();if(!config.has("sd_fmt_req"))return;const String requested=config.getString("sd_fmt_req");
    // Consume first. Power loss, wrong card, recovery and errors never replay the request.
    if(!config.remove("sd_fmt_req")){sdFormatState_=3;return;}
    String drive,token;FATFS* filesystem=nullptr;
    if(!bootHealthy||esp_reset_reason()!=ESP_RST_SW||!sdReady_||requested.length()<66||requested.length()>75||!sdFormatTarget(drive,token,filesystem)||requested!=String(config.getU32("boot_count"))+":"+token){sdFormatState_=3;return;}
    void* scratch=heap_caps_malloc(4096,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!scratch){sdFormatState_=3;return;}
    // Only the proven SD drive, before any application filesystem users exist.
    const bool unmounted=f_mount(nullptr,drive.c_str(),0)==FR_OK;MKFS_PARM options{};options.fmt=FM_ANY;
    const bool formatted=unmounted&&f_mkfs(drive.c_str(),&options,scratch,4096)==FR_OK;
    const bool remounted=unmounted&&f_mount(filesystem,drive.c_str(),1)==FR_OK;heap_caps_free(scratch);
    sdFormatState_=formatted&&remounted?2:3;reportSdMounted(remounted,remounted?SD_MMC.totalBytes():0);
    if(remounted)prepareExternalDirectories(SD_MMC);
}
fs::FS* StorageService::sdUserDataFs(){return sdReady_?static_cast<fs::FS*>(&SD_MMC):nullptr;}
uint64_t StorageService::sdFreeBytes(){if(!sdReady_)return 0;const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();return total>used?total-used:0;}
String StorageService::sdUserDataPath(const String& relative)const {if(!validateRelativePath(relative))return {};return String(kSdRoot)+"/data"+(relative.startsWith("/")?relative:"/"+relative);}
bool StorageService::truncateSdUserData(const String& relative,size_t size){if(!sdReady_)return false;const String path=sdUserDataPath(relative);if(!path.length())return false;return ::truncate((String("/sdcard")+path).c_str(),size)==0;}
bool StorageService::touchSdUserData(const String& relative){if(!sdReady_)return false;const String path=sdUserDataPath(relative);return path.length()&&::utime((String("/sdcard")+path).c_str(),nullptr)==0;}
bool StorageService::partitionLooksBlank(const char* label) const {
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, label);
    if (!p) return false;
    // An erased header is not proof that the filesystem is empty.
    uint8_t bytes[512];
    for (size_t offset = 0; offset < p->size; offset += sizeof(bytes)) {
        const size_t count = (p->size - offset < sizeof(bytes))
            ? p->size - offset : sizeof(bytes);
        if (esp_partition_read(p, offset, bytes, count) != ESP_OK) return false;
        for (size_t i = 0; i < count; ++i) if (bytes[i] != 0xff) return false;
        taskYIELD();
    }
    return true;
}
bool StorageService::mountInternalWithoutDestructiveRecovery(){return LittleFS.begin(false,"/system",10,"systemdata");}
bool StorageService::mountAppFs(){
    // Own partition so app install/update/remove never needs a firmware OTA.
    if(appFs_.begin(false,"/apps-root",10,"appfs")) return true;
    // Only format a genuinely blank (never-formatted) partition, never a damaged one.
    if(!partitionLooksBlank("appfs")) return false;
    return appFs_.begin(true,"/apps-root",10,"appfs");
}
bool StorageService::begin(){
    internalReady_=mountInternalWithoutDestructiveRecovery();
    appFsReady_=mountAppFs();
    sdReady_=false;
    sdBytes_=0;

    if(internalReady_){
        recoverAtomicWrite(StorageArea::Config,"/config/state.bin");
        recoverAtomicWrite(StorageArea::Recovery,"/update/journal.bin");
    }

    if(SD_MMC.setPins(
        board::kSdClk,
        board::kSdCmd,
        board::kSdD0,
        board::kSdD1,
        board::kSdD2,
        board::kSdD3
    )){
        // An absent card is a supported normal state. Keep the ESP-IDF and
        // Arduino SD drivers from reporting the probe failure as an error.
        esp_log_level_set("sdmmc", ESP_LOG_WARN);
        esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_WARN);
        esp_log_level_set("SD_MMC", ESP_LOG_WARN);
        const bool mounted = SD_MMC.begin("/sdcard",false,false,SDMMC_FREQ_DEFAULT,10);
        const bool present = mounted && SD_MMC.cardType()!=CARD_NONE;
        esp_log_level_set("sdmmc", ESP_LOG_ERROR);
        esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_ERROR);
        esp_log_level_set("SD_MMC", ESP_LOG_ERROR);
        if(present){
            reportSdMounted(true,SD_MMC.totalBytes());
            // Apps can alternatively be installed from the SD card without a firmware flash.
            prepareExternalDirectories(SD_MMC);
        }
    }

    return internalReady_;
}
void StorageService::reportSdMounted(bool m,uint64_t cap){if(sdReady_==m&&(!m||sdBytes_==cap))return;notifyMutation();sdReady_=m;sdBytes_=m?cap:0;EventBus::instance().publish({m?EventType::SdInserted:EventType::SdRemoved,nullptr});}
bool StorageService::canStore(StorageClass c)const{switch(c){case StorageClass::UsbAppPackage:return UsbStorageService::instance().available()&&!UsbStorageService::instance().formatBusy();case StorageClass::SystemData:return internalReady_;case StorageClass::AppPackage:return appFsReady_;case StorageClass::UserData:return sdReady_||internalReady_;case StorageClass::Media:case StorageClass::Logs:case StorageClass::VoiceModels:return sdReady_;}return false;}
bool StorageService::usesSd(StorageClass c)const{return sdReady_&&(c==StorageClass::UserData||c==StorageClass::Media||c==StorageClass::Logs||c==StorageClass::VoiceModels);}
fs::FS* StorageService::fsFor(StorageClass c){if(c==StorageClass::UsbAppPackage)return canStore(c)?&usbAppFileSystem():nullptr;if(usesSd(c))return &SD_MMC;switch(c){case StorageClass::AppPackage:return appFsReady_?static_cast<fs::FS*>(&appFs_):nullptr;case StorageClass::SystemData:case StorageClass::UserData:return internalReady_?&LittleFS:nullptr;default:return nullptr;}}
String StorageService::pathFor(StorageClass c,const String&r)const{if(!validateRelativePath(r))return{};String rel=r;if(!rel.startsWith("/"))rel="/"+rel;if(c==StorageClass::UsbAppPackage)return rel;if(usesSd(c)){switch(c){case StorageClass::UserData:return String(kSdRoot)+"/data"+rel;case StorageClass::Media:return String(kSdRoot)+"/media"+rel;case StorageClass::Logs:return String(kSdRoot)+"/logs"+rel;case StorageClass::VoiceModels:return String(kSdRoot)+"/voice"+rel;default:break;}}if(c==StorageClass::SystemData)return rel;if(c==StorageClass::AppPackage)return rel;if(c==StorageClass::UserData)return String(kInternalFallbackRoot)+rel;return{};}
bool StorageService::validateRelativePath(const String& p) const {
    if (!p.length() || p.length() > 180 || p.indexOf('\\') >= 0) return false;
    size_t start = 0;
    for (size_t i = 0; i <= p.length(); ++i) {
        if (i < p.length() && static_cast<unsigned char>(p[i]) < 32) return false;
        if (i == p.length() || p[i] == '/') {
            const String part = p.substring(start, i);
            if (part == "." || part == "..") return false;
            start = i + 1;
        }
    }
    return true;
}
fs::FS* StorageService::fsFor(StorageArea a){switch(a){case StorageArea::UserData:if(sdReady_)return &SD_MMC;return internalReady_?static_cast<fs::FS*>(&LittleFS):nullptr;case StorageArea::AppData:return appFsReady_?static_cast<fs::FS*>(&appFs_):nullptr;case StorageArea::SystemData:case StorageArea::Cache:case StorageArea::Config:case StorageArea::Recovery:return internalReady_?static_cast<fs::FS*>(&LittleFS):nullptr;}return nullptr;}
String StorageService::normalize(StorageArea a,const String&r)const{if(!validateRelativePath(r))return{};String rel=r;if(!rel.startsWith("/"))rel="/"+rel;switch(a){case StorageArea::UserData:return sdReady_?String(kSdRoot)+"/data"+rel:String(kInternalFallbackRoot)+rel;case StorageArea::AppData:return rel;case StorageArea::SystemData:return String("/systemdata")+rel;case StorageArea::Cache:return String("/cache")+rel;case StorageArea::Config:return String("/config")+rel;case StorageArea::Recovery:return String("/recovery")+rel;}return{};}
String StorageService::pathFor(StorageArea a,const String&r)const{return normalize(a,r);}
bool StorageService::prepareExternalDirectories(fs::FS& fs){
    bool changed=false;
    const bool ok=ensureExternalStorageLayout([&](const char* path){
        if(fs.exists(path)){File existing=fs.open(path,FILE_READ);const bool directory=existing&&existing.isDirectory();existing.close();return directory;}
        if(!fs.mkdir(path)){ESP_LOGW("STORAGE","Cannot prepare directory %s; existing data preserved",path);return false;}
        changed=true;return true;
    });
    if(changed)notifyMutation();
    return ok;
}
static bool ensureDirs(fs::FS*fs,const String&path){int slash=path.lastIndexOf('/');if(slash<=0)return true;String dir=path.substring(0,slash),cur;int start=1;while(start<=dir.length()){int next=dir.indexOf('/',start);if(next<0)next=dir.length();cur+="/"+dir.substring(start,next);if(cur.length()>1&&!fs->exists(cur)&&!fs->mkdir(cur))return false;start=next+1;}return true;}
bool StorageService::ensureParentDirs(StorageClass c,const String&r){auto*fs=fsFor(c);String p=pathFor(c,r);return fs&&p.length()&&ensureDirs(fs,p);}
bool StorageService::ensureSdUserDataParents(const String& relative){auto* fs=sdUserDataFs();const String path=sdUserDataPath(relative);return fs&&path.length()&&ensureDirs(fs,path);}
bool StorageService::ensureParentDirs(StorageArea a,const String&r){auto*fs=fsFor(a);String p=pathFor(a,r);return fs&&p.length()&&ensureDirs(fs,p);}
bool StorageService::read(StorageArea a,const String&r,void*d,size_t n,size_t*actual){uint32_t st=micros();if(!d||!n)return false;auto*fs=fsFor(a);String p=pathFor(a,r);if(!fs||!p.length()){++readErrors_;return false;}File f=fs->open(p,FILE_READ);if(!f){++readErrors_;return false;}size_t got=f.read((uint8_t*)d,n);f.close();if(actual)*actual=got;uint32_t us=micros()-st;if(us>maxReadUs_)maxReadUs_=us;return true;}
bool StorageService::write(StorageArea a,const String&r,const void*d,size_t n){notifyMutation();uint32_t st=micros();if((!d&&n)||!ensureParentDirs(a,r)){++writeErrors_;return false;}auto*fs=fsFor(a);String p=pathFor(a,r);File f=fs->open(p,FILE_WRITE);if(!f){++writeErrors_;return false;}size_t got=n?f.write((const uint8_t*)d,n):0;f.flush();f.close();if(got!=n){++writeErrors_;return false;}uint32_t us=micros()-st;if(us>maxWriteUs_)maxWriteUs_=us;return true;}
bool StorageService::writeAtomic(StorageArea a,const String&r,const void*d,size_t n,const Validator&v){notifyMutation();if(!d&&n){++writeErrors_;return false;}auto*fs=fsFor(a);String target=pathFor(a,r);if(!fs||!target.length()||!ensureParentDirs(a,r)){++writeErrors_;return false;}String tmp=target+".tmp",bak=target+".bak";fs->remove(tmp);File f=fs->open(tmp,FILE_WRITE);if(!f){++writeErrors_;return false;}size_t got=n?f.write((const uint8_t*)d,n):0;f.flush();f.close();if(got!=n||(v&&!v(*fs,tmp))){fs->remove(tmp);++writeErrors_;return false;}fs->remove(bak);if(fs->exists(target)&&!fs->rename(target,bak)){fs->remove(tmp);++writeErrors_;return false;}if(!fs->rename(tmp,target)){if(fs->exists(bak))fs->rename(bak,target);fs->remove(tmp);++writeErrors_;return false;}if(v&&!v(*fs,target)){fs->remove(target);if(fs->exists(bak))fs->rename(bak,target);++writeErrors_;return false;}fs->remove(bak);return true;}
bool StorageService::recoverAtomicWrite(StorageArea a,const String&r,const Validator&v){auto*fs=fsFor(a);String target=pathFor(a,r);if(!fs||!target.length())return false;String tmp=target+".tmp",bak=target+".bak";auto valid=[&](const String&p){return fs->exists(p)&&(!v||v(*fs,p));};if(valid(target)){fs->remove(tmp);fs->remove(bak);return true;}if(valid(bak)){fs->remove(target);bool ok=fs->rename(bak,target);fs->remove(tmp);return ok;}if(valid(tmp)){fs->remove(target);bool ok=fs->rename(tmp,target);fs->remove(bak);return ok;}fs->remove(tmp);return false;}
bool StorageService::remove(StorageArea a,const String&r){notifyMutation();auto*fs=fsFor(a);String p=pathFor(a,r);return fs&&p.length()&&(!fs->exists(p)||fs->remove(p));}
bool StorageService::removeTree(StorageArea a,const String&r){notifyMutation();auto*fs=fsFor(a);String root=pathFor(a,r);if(!fs||!root.length())return false;std::function<bool(const String&)> erase=[&](const String&p)->bool{File dir=fs->open(p);if(!dir)return true;if(!dir.isDirectory()){dir.close();return fs->remove(p);}File f=dir.openNextFile();while(f){String child=f.path();bool isDir=f.isDirectory();f.close();if(isDir){if(!erase(child)){dir.close();return false;}}else if(!fs->remove(child)){dir.close();return false;}f=dir.openNextFile();}dir.close();return fs->rmdir(p);};return erase(root);}
bool StorageService::rename(StorageArea a,const String&from,const String&to){notifyMutation();auto*fs=fsFor(a);String f=pathFor(a,from),t=pathFor(a,to);return fs&&f.length()&&t.length()&&ensureParentDirs(a,to)&&fs->rename(f,t);}
bool StorageService::exists(StorageArea a,const String&r){auto*fs=fsFor(a);String p=pathFor(a,r);return fs&&p.length()&&fs->exists(p);}
bool StorageService::writeBinaryAtomic(StorageClass c,const String&r,const void*d,size_t n){if(c==StorageClass::UsbAppPackage)return false;/* USB packages use the generation-bound installer transaction, never UserData fallback. */StorageArea a=c==StorageClass::SystemData?StorageArea::SystemData:(c==StorageClass::AppPackage?StorageArea::AppData:StorageArea::UserData);return writeAtomic(a,r,d,n);}
bool StorageService::writeTextAtomic(StorageClass c,const String&r,const String&t){return writeBinaryAtomic(c,r,t.c_str(),t.length());}
size_t StorageService::readBinary(StorageClass c,const String&r,void*d,size_t n){auto*fs=fsFor(c);if(!fs||!d||!n)return 0;File f=fs->open(pathFor(c,r),FILE_READ);if(!f){++readErrors_;return 0;}size_t got=f.read((uint8_t*)d,n);f.close();return got;}
bool StorageService::exists(StorageClass c,const String&r){auto*fs=fsFor(c);return fs&&fs->exists(pathFor(c,r));}
bool StorageService::sdAbsoluteExists(const String&p){return sdReady_&&validateRelativePath(p)&&SD_MMC.exists(p);}
File StorageService::openSdRead(const String&p){ if(!sdReady_||!validateRelativePath(p)) return File(); return SD_MMC.open(p,FILE_READ);}
File StorageService::openSdDirectory(const String&p){ if(!sdReady_||!validateRelativePath(p)) return File(); File f=SD_MMC.open(p); return (f&&f.isDirectory())?f:File(); }
bool StorageService::ensureSdDirectory(const String& path){if(!sdReady_||!validateRelativePath(path)||!path.startsWith("/"))return false;
    if(SD_MMC.exists(path)){File directory=SD_MMC.open(path,FILE_READ);return directory&&directory.isDirectory();}return SD_MMC.mkdir(path);}
bool StorageService::suspendInternalForUpdate(){ if(!internalReady_) return true; LittleFS.end(); internalReady_=false; return true; }
bool StorageService::resumeInternalAfterUpdate(){ internalReady_=mountInternalWithoutDestructiveRecovery(); return internalReady_; }
bool StorageService::formatSystemDataForRecovery(){LittleFS.end();if(!LittleFS.begin(true,"/system",10,"systemdata")){internalReady_=false;return false;}internalReady_=true;return true;}
}
