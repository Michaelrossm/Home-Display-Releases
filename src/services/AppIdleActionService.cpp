#include "AppIdleActionService.h"
#include "ConfigService.h"
#include "IdleService.h"
#include "SecurityService.h"
#include "../core/AppManager.h"
#include "../ui/UiShell.h"
#include <ArduinoJson.h>
namespace hp {
AppIdleActionService& AppIdleActionService::instance(){static AppIdleActionService service;return service;}
bool AppIdleActionService::configure(const String& owner,uint32_t after,const String& arguments,bool enabled){
    if(owner.length()>31||arguments.length()>256||after<10||after>86400)return false;
    JsonDocument args;if(deserializeJson(args,arguments,DeserializationOption::NestingLimit(4))||!args.is<JsonObject>())return false;
    SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    JsonDocument record;record["owner"]=owner;record["after"]=after;record["arguments"]=args;record["enabled"]=enabled;String json;serializeJson(record,json);
    const bool saved=ConfigService::instance().putString("idle_action",json);if(saved)dirty_=true;return saved;
}
void AppIdleActionService::poll(){auto& idle=IdleService::instance();auto& apps=AppManager::instance();
    if(running_&&(!apps.current()||owner_!=apps.current()->id())){running_=false;idle.activity();}
    if(running_&&!idle.idleActionActive()){
        running_=false;if(apps.current()&&owner_==apps.current()->id())UiShell::instance().showHome();
    }
    const uint32_t now=millis();if(uint32_t(now-lastCheck_)<1000)return;lastCheck_=now;
    if(dirty_.exchange(false)){JsonDocument record;const String json=ConfigService::instance().getString("idle_action","");
        owner_="";after_=0;arguments_="{}";
        if(!deserializeJson(record,json)&&bool(record["enabled"]|false)){owner_=record["owner"].as<String>();after_=record["after"]|0u;arguments_="";serializeJson(record["arguments"],arguments_);}
    }
    if(running_||!owner_.length()||after_<10||after_>86400||SecurityService::instance().tabletLocked())return;
    // Auto-launch only from Home: never interrupt another app, media or PIN UI.
    if(apps.current()||uint32_t(now-idle.lastActivityMillis())/1000<after_||(haveFired_&&lastFired_==idle.lastActivityMillis()))return;
    auto* app=apps.find(owner_.c_str());if(!app||!apps.isEnabled(app)||!(app->permissions()&perm(AppPermission::IdleIntegration)))return;
    lastFired_=idle.lastActivityMillis();haveFired_=true;
    if(apps.openWithArguments(owner_.c_str(),UiShell::instance().content(),arguments_)){idle.beginIdleAction();running_=true;}
}
}
