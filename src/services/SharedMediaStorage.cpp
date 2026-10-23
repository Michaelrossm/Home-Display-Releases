#include "SharedMediaStorage.h"
#include "StorageService.h"
#include "UsbStorageService.h"
#include "ConfigService.h"
#include "SecurityService.h"
#include <sys/stat.h>
#include <errno.h>
namespace hp {
namespace {
constexpr const char* kDirs[]={"Bilder","Musik","Videos","Dokumente","Downloads","Backups","Apps","Daten"};
bool makeUsbDir(const String& path){
 const String full=String(UsbStorageService::mountPath())+"/"+path;
 if(::mkdir(full.c_str(),0775)==0)return true;
 if(errno!=EEXIST)return false;
 struct stat s{};return ::stat(full.c_str(),&s)==0&&S_ISDIR(s.st_mode);
}
}
SharedMediaStorage& SharedMediaStorage::instance(){static SharedMediaStorage s;return s;}
const char* const* SharedMediaStorage::folders(){return kDirs;}
size_t SharedMediaStorage::folderCount(){return sizeof(kDirs)/sizeof(kDirs[0]);}
String SharedMediaStorage::preference() const {
 auto& config=ConfigService::instance();const uint32_t revision=config.revision();
 uint64_t cached=cachedPreference_.load(std::memory_order_acquire);
 if(!(cached>>63)||uint32_t(cached>>2)!=revision){
     const String value=config.getString("media_primary","auto");
     const unsigned mode=value=="sd"?1:value=="usb"?2:0;
     cached=(uint64_t(1)<<63)|(uint64_t(revision)<<2)|mode;
     // Version and selection are published together; concurrent web/display
     // configuration changes invalidate this cache via the central revision.
     cachedPreference_.store(cached,std::memory_order_release);
 }
 return (cached&3)==1?String("sd"):(cached&3)==2?String("usb"):String("auto");
}
bool SharedMediaStorage::setPreference(const String& p){
 if(p!="auto"&&p!="sd"&&p!="usb")return false;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 SecurityWriteGuard guard;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 return ConfigService::instance().putString("media_primary",p);
}
String SharedMediaStorage::primaryUri() const {
 const bool sd=StorageService::instance().sdAvailable(),usb=UsbStorageService::instance().available();
 const String p=preference();
 if(p=="usb"&&usb)return "usb:/";
 if(p=="sd"&&sd)return "sd:/";
 if(sd)return "sd:/";
 if(usb)return "usb:/";
 return {};
}
bool SharedMediaStorage::prepareSdFolders() const {
 if(!StorageService::instance().sdAvailable())return false;
 bool ok=true;for(const char* dir:kDirs){String path=String("/")+dir;
 if(!StorageService::instance().ensureSdDirectory(path))ok=false;
 }return ok;
}
bool SharedMediaStorage::prepareUsbFolders() const {
 UsbFilesGuard guard;if(!guard)return false;
 if(!UsbStorageService::instance().available())return false;
 bool ok=true;for(const char* dir:kDirs)if(!makeUsbDir(dir))ok=false;
 return ok;
}
}
