#include "IntercomSettingsApi.h"
#include "../goliath/GoliathSettings.h"
#include "../goliath/GoliathService.h"
#include "../../core/AppManager.h"
#include "../../services/IntercomService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/SecurityService.h"
#include <cstring>
namespace hp {
size_t nativeIntercomDevices(IntercomDevice* out,size_t capacity){
    if(!out||!capacity)return 0;
    size_t count=IntercomService::instance().devices("intercom",out,capacity);
    const auto old=GoliathSettings::instance().load();
    if(old.host.length()&&count<capacity){auto& d=out[count++];d={};
        strlcpy(d.id,legacyIntercomId,sizeof(d.id));strlcpy(d.owner,"intercom",sizeof(d.owner));
        strlcpy(d.provider,"dahua",sizeof(d.provider));strlcpy(d.name,"Goliath (Bestandskonfiguration)",sizeof(d.name));
        strlcpy(d.host,old.host.c_str(),sizeof(d.host));strlcpy(d.username,old.username.c_str(),sizeof(d.username));
        strlcpy(d.participant,old.apartment.c_str(),sizeof(d.participant));
        strlcpy(d.videoPath,old.useExtraStream?"/cam/realmonitor?channel=1&subtype=1":"/cam/realmonitor?channel=1&subtype=0",sizeof(d.videoPath));
        d.port=old.httpPort;d.videoPort=old.rtspPort;d.enabled=old.enabled;
    }
    return count;
}
bool saveNativeIntercomDevice(const IntercomDevice& d,const String* password,String& error){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){error="pin_locked";return false;}
    if(!memchr(d.id,0,sizeof(d.id))||!memchr(d.owner,0,sizeof(d.owner))||!memchr(d.provider,0,sizeof(d.provider))||
        !memchr(d.host,0,sizeof(d.host))||!memchr(d.name,0,sizeof(d.name))||!memchr(d.username,0,sizeof(d.username))||
        !memchr(d.participant,0,sizeof(d.participant))||!memchr(d.videoPath,0,sizeof(d.videoPath))){error="device_fields";return false;}
    if(strcmp(d.owner,"intercom")){error="owner_mismatch";return false;}
    if(!d.name[0]||!d.host[0]||!d.fps||d.fps>10||(password&&password->length()>192)){error="device_fields";return false;}
    if(strcmp(d.id,legacyIntercomId))return IntercomService::instance().configure(d,password,error);
    if(strcmp(d.provider,"dahua")||!d.port||!d.videoPort){error="legacy_options";return false;}
    auto old=GoliathSettings::instance().load();old.host=d.host;old.username=d.username;old.apartment=d.participant;
    old.httpPort=d.port;old.rtspPort=d.videoPort;old.enabled=d.enabled;if(password)old.password=*password;
    if(!GoliathSettings::instance().save(old)){error="config_write_failed";return false;}
    GoliathService::instance().settingsChanged();return true;
}
bool removeNativeIntercomDevice(const char* id,String& error){
    if(!id||!strcmp(id,legacyIntercomId)){error="legacy_delete_denied";return false;}
    return IntercomService::instance().removeDevice("intercom",id,error);
}
bool startNativeIntercomVideo(const IntercomDevice& d,String& error){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked()||!AppManager::instance().isEnabled("intercom")){error="video_unavailable";return false;}
    if(!strcmp(d.id,legacyIntercomId))return GoliathService::instance().startCamera(error);
    return IntercomService::instance().startVideo("intercom",d.id,error);
}
}
