#include "AppRegistry.h"
#include "../apps/browser/WebAppManager.h"
#include "../apps/lua/LuaAppRegistry.h"
#include "../services/HomeAppOrderService.h"
#include <cstdio>

namespace hp {

AppRegistry& AppRegistry::instance(){ static AppRegistry r; return r; }

SemaphoreHandle_t AppRegistry::mutex() const {
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    return mutex_;
}

String AppRegistry::webStableId(uint32_t id){
    char buf[16]; snprintf(buf,sizeof(buf),"web-%08lx",(unsigned long)id); return String(buf);
}
const char* AppRegistry::kindName(AppKind kind){
    switch(kind){case AppKind::Native:return "Native";case AppKind::Lua:return "Lua";case AppKind::Web:return "Web";case AppKind::System:return "System";}return "?";
}
const char* AppRegistry::stateName(AppRegistryState state){
    switch(state){case AppRegistryState::Enabled:return "Aktiv";case AppRegistryState::Disabled:return "Deaktiviert";case AppRegistryState::Error:return "Fehler";}return "?";
}

void AppRegistry::rebuild(){
    // Rebuild directly into the persistent registry storage. The previous
    // implementation used AppRegistryEntry next[48] on the task stack; the
    // launcher reorder code then added more 48-entry arrays and could overflow
    // the UI/main task during boot.
    auto m=mutex();
    if(m)xSemaphoreTake(m,portMAX_DELAY);

    count_=0;

    auto& apps=AppManager::instance();
    for(size_t i=0;i<apps.count()&&count_<kMaxEntries;++i){
        auto* app=apps.at(i); if(!app)continue;
        auto& e=entries_[count_++]; e={};
        e.id=app->id();e.name=app->name();e.version=app->version();e.icon=app->icon();
        e.kind=app->kind();e.userInstallable=app->userInstallable();
        e.launcherVisible=app->launcherVisible();
        e.removable=app->kind()==AppKind::Lua&&LuaAppRegistry::instance().owns(app->id());
        e.hasSettings=app->hasSettings();e.webPath=app->webPath();e.app=app;
        e.permissions=app->permissions();
        const auto runtime=apps.runtimeStatus(app->id());
        if(runtime.state==AppRuntimeState::Error){
            e.state=AppRegistryState::Error;
            if(runtime.detail.length())e.detail=runtime.detail;
        }else{
            e.state=(!app->userInstallable()||apps.isEnabled(app))
                ?AppRegistryState::Enabled:AppRegistryState::Disabled;
        }
    }

    auto& web=WebAppManager::instance();
    for(size_t i=0;i<web.count()&&count_<kMaxEntries;++i){
        const auto* r=web.at(i);if(!r)continue;
        auto& e=entries_[count_++];e={};
        e.id=webStableId(r->id);e.name=r->name;e.detail=r->url;e.icon=r->icon;
        e.webPath=r->url;
        e.kind=AppKind::Web;e.userInstallable=true;e.launcherVisible=true;e.removable=true;e.backendIndex=i;
        e.state=r->enabled?AppRegistryState::Enabled:AppRegistryState::Disabled;
    }

    HomeAppOrderService::instance().sortEntries(entries_,count_);

    if(m)xSemaphoreGive(m);
}

size_t AppRegistry::snapshot(AppRegistryEntry* out,size_t capacity) const{
    if(!out||!capacity)return 0;auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    const size_t n=min(capacity,count_);for(size_t i=0;i<n;++i)out[i]=entries_[i];
    if(m)xSemaphoreGive(m);return n;
}
bool AppRegistry::copyAt(size_t index,AppRegistryEntry& out) const{
    auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    const bool ok=index<count_;
    if(ok)out=entries_[index];
    if(m)xSemaphoreGive(m);
    return ok;
}

String AppRegistry::userInstallableIdList() const{
    return idList(false);
}
String AppRegistry::launcherIdList() const{
    return idList(true);
}
String AppRegistry::idList(bool launcher) const{
    String out;out.reserve(512);
    auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    for(size_t i=0;i<count_;++i){
        if(!(launcher?entries_[i].launcherVisible:entries_[i].userInstallable))continue;
        if(out.length())out+='|';
        out+=entries_[i].id;
    }
    if(m)xSemaphoreGive(m);
    return out;
}

bool AppRegistry::lookup(const char* id,AppRegistryEntry& out) const{
    if(!id||!id[0])return false;auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    bool found=false;for(size_t i=0;i<count_;++i)if(entries_[i].id==id){out=entries_[i];found=true;break;}
    if(m)xSemaphoreGive(m);return found;
}
const AppRegistryEntry* AppRegistry::find(const char* id) const{
    if(!id||!id[0])return nullptr;
    for(size_t i=0;i<count_;++i)if(entries_[i].id==id)return &entries_[i];
    return nullptr;
}

bool AppRegistry::setEnabled(const char* id,bool enabled){
    AppRegistryEntry e;if(!lookup(id,e)||!e.userInstallable)return false;
    bool ok=false;if(e.kind==AppKind::Web)ok=WebAppManager::instance().setEnabled(e.backendIndex,enabled);
    else if(e.app)ok=AppManager::instance().setEnabled(e.app,enabled);
    if(!ok)return false;
    auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    for(size_t i=0;i<count_;++i)if(entries_[i].id==id){entries_[i].state=enabled?AppRegistryState::Enabled:AppRegistryState::Disabled;break;}
    if(m)xSemaphoreGive(m);return true;
}

bool AppRegistry::removeWeb(const char* id){
    AppRegistryEntry e;if(!lookup(id,e)||e.kind!=AppKind::Web||e.backendIndex==SIZE_MAX)return false;
    if(!WebAppManager::instance().remove(e.backendIndex))return false;
    auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    size_t pos=SIZE_MAX;for(size_t i=0;i<count_;++i)if(entries_[i].id==id){pos=i;break;}
    if(pos!=SIZE_MAX){for(size_t i=pos+1;i<count_;++i)entries_[i-1]=entries_[i];--count_;
        for(size_t i=0;i<count_;++i)if(entries_[i].kind==AppKind::Web&&entries_[i].backendIndex>e.backendIndex)--entries_[i].backendIndex;}
    if(m)xSemaphoreGive(m);return true;
}

bool AppRegistry::addWeb(const char* name,const char* url){
    auto& web=WebAppManager::instance();if(!web.add(name,url))return false;
    const size_t idx=web.count()?web.count()-1:0;const auto* r=web.at(idx);if(!r)return true;
    auto m=mutex();if(m)xSemaphoreTake(m,portMAX_DELAY);
    if(count_<kMaxEntries){auto& e=entries_[count_++];e={};e.id=webStableId(r->id);e.name=r->name;e.detail=r->url;e.icon=r->icon;
        e.kind=AppKind::Web;e.userInstallable=true;e.launcherVisible=true;e.removable=true;e.backendIndex=idx;e.state=r->enabled?AppRegistryState::Enabled:AppRegistryState::Disabled;}
    if(m)xSemaphoreGive(m);return true;
}
}
