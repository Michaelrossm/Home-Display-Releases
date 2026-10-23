#include "LuaNavigationBridge.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include "LuaApp.h"
#include "LuaJobService.h"
#include "LuaBackgroundRun.h"
#include "../../ui/UiShell.h"
#include "../../core/AppBackgroundScheduler.h"
#include "../../services/NetworkService.h"
namespace hp {
namespace {
int webSettingsCapabilities(lua_State* L){
    lua_newtable(L);lua_pushinteger(L,1);lua_setfield(L,-2,"version");
    lua_pushinteger(L,1);lua_setfield(L,-2,"schema");lua_pushboolean(L,true);lua_setfield(L,-2,"shared_server");
    lua_pushboolean(L,true);lua_setfield(L,-2,"shared_kv");lua_pushboolean(L,true);lua_setfield(L,-2,"csrf");
    lua_pushboolean(L,true);lua_setfield(L,-2,"on_demand");
    lua_pushboolean(L,false);lua_setfield(L,-2,"background_tasks");
    lua_pushboolean(L,false);lua_setfield(L,-2,"custom_html");return 1;
}
int webSettingsUrl(lua_State* L){auto* app=LuaApp::self(L);const String ip=NetworkService::instance().ip();
    if(!app||!ip.length()||ip=="0.0.0.0"){lua_pushnil(L);lua_pushstring(L,"network_offline");return 2;}
    const String url=String("http://")+ip+"/?app_settings="+app->id();lua_pushstring(L,url.c_str());return 1;
}
int backgroundCapabilities(lua_State* L){lua_newtable(L);lua_pushinteger(L,1);lua_setfield(L,-2,"version");lua_pushboolean(L,true);lua_setfield(L,-2,"policy");lua_pushboolean(L,true);lua_setfield(L,-2,"callback");lua_pushboolean(L,true);lua_setfield(L,-2,"lua_callback");lua_pushinteger(L,kLuaBackgroundUs/1000);lua_setfield(L,-2,"max_callback_ms");lua_pushinteger(L,kLuaBackgroundInstructions);lua_setfield(L,-2,"instruction_budget");lua_pushinteger(L,3);lua_setfield(L,-2,"max_suspended");
    const auto runtime=AppManager::instance().luaDiagnostics();
    lua_pushinteger(L,runtime.foreground);lua_setfield(L,-2,"foreground");lua_pushinteger(L,runtime.suspended);lua_setfield(L,-2,"suspended");lua_pushinteger(L,runtime.evicted);lua_setfield(L,-2,"evicted");lua_pushinteger(L,runtime.evictions);lua_setfield(L,-2,"evictions");
    AppBackgroundScheduler::Diagnostics d;if(AppBackgroundScheduler::instance().tryDiagnostics(d)){lua_pushinteger(L,d.policies);lua_setfield(L,-2,"policies");lua_pushinteger(L,d.executed);lua_setfield(L,-2,"executed");lua_pushinteger(L,d.deferredHigh);lua_setfield(L,-2,"deferred_high");lua_pushinteger(L,d.deferredOffline);lua_setfield(L,-2,"deferred_offline");lua_pushinteger(L,d.maxCallbackUs);lua_setfield(L,-2,"max_callback_us");}return 1;
}
int backgroundRequest(lua_State* L){auto* app=LuaApp::self(L);if(!app||app->settingsState()||!lua_istable(L,1)){lua_pushboolean(L,false);return 1;}
    bool valid=true;AppBackgroundPolicy p;
    lua_getfield(L,1,"priority");const char* text=lua_tostring(L,-1);if(text&&!strcmp(text,"low"))p.priority=AppBackgroundPriority::Low;else if(text&&!strcmp(text,"normal"))p.priority=AppBackgroundPriority::Normal;else if(text&&!strcmp(text,"important"))p.priority=AppBackgroundPriority::Important;else if(!text||strcmp(text,"none"))valid=false;lua_pop(L,1);
    auto seconds=[&](const char* key,uint32_t fallback){lua_getfield(L,1,key);uint32_t result=fallback;if(!lua_isnil(L,-1)){if(!lua_isinteger(L,-1)||lua_tointeger(L,-1)<1||lua_tointeger(L,-1)>604800)valid=false;else result=static_cast<uint32_t>(lua_tointeger(L,-1))*1000;}lua_pop(L,1);return result;};
    p.preferredIntervalMs=seconds("preferred_interval",1800000);p.maximumDelayMs=seconds("maximum_delay",3600000);
    auto flag=[&](const char* key){lua_getfield(L,1,key);if(!lua_isnil(L,-1)&&!lua_isboolean(L,-1))valid=false;const bool result=lua_toboolean(L,-1);lua_pop(L,1);return result;};p.requiresNetwork=flag("network");p.allowDuringHighLoad=flag("allow_high_load");
    const bool saved=valid&&validBackgroundPolicy(p)&&AppBackgroundScheduler::instance().request(app->id(),p);
    if(saved&&p.priority==AppBackgroundPriority::None)LuaJobService::instance().cancelOwner(app->id());
    lua_pushboolean(L,saved);return 1;
}
int bridgeOpenSettings(lua_State* L) {
    auto* app=LuaApp::self(L);
    if(app&&app->lifecycle()!=LuaRuntimeState::Foreground&&app->lifecycle()!=LuaRuntimeState::Opening)app=nullptr;
    lua_pushboolean(L,app&&app->hasSettings()&&UiShell::instance().requestAppSettings(app->id()));
    return 1;
}
int bridgeOpenUsers(lua_State* L){
    auto* app=LuaApp::self(L);const bool visible=app&&(app->lifecycle()==LuaRuntimeState::Foreground||app->lifecycle()==LuaRuntimeState::Opening);
    lua_pushboolean(L,visible&&UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}"));return 1;
}
int bridgeOpenApp(lua_State* L) {
    auto* app=LuaApp::self(L);
    if(app&&app->lifecycle()!=LuaRuntimeState::Foreground&&app->lifecycle()!=LuaRuntimeState::Opening)app=nullptr;
    if(app&&lua_istable(L,1)){
        lua_getglobal(L,"hp_json_encode");lua_pushvalue(L,1);lua_call(L,1,1);
        const char* arguments=lua_tostring(L,-1);
        const bool queued=arguments&&UiShell::instance().requestAppOpen(app->id(),arguments);
        lua_pushboolean(L,queued);return 1;
    }
    lua_pushboolean(L,app&&UiShell::instance().requestAppOpen(app->id()));
    return 1;
}
int arguments(lua_State* L){auto* app=LuaApp::self(L);lua_getglobal(L,"hp_json_decode");
    lua_pushstring(L,app?app->launchArguments().c_str():"{}");lua_call(L,1,1);return 1;}
int launch(lua_State* L){auto* app=LuaApp::self(L);const char* id=luaL_checkstring(L,1);
    if(!app||strcmp(id,app->id()))return luaL_error(L,"only own app may be launched");
    lua_remove(L,1);return bridgeOpenApp(L);
}


}
void registerLuaNavigationBridge(lua_State* L) {
    lua_register(L,"hp_open_app_settings",bridgeOpenSettings);
    lua_register(L,"hp_open_app",bridgeOpenApp);
    lua_register(L,"hp_open_user_settings",bridgeOpenUsers);
    lua_register(L,"hp_app_arguments",arguments);
    lua_register(L,"hp_app_launch",launch);
    lua_register(L,"hp_background_capabilities",backgroundCapabilities);
    lua_register(L,"hp_background_request",backgroundRequest);
    lua_register(L,"hp_web_settings_capabilities",webSettingsCapabilities);
    lua_register(L,"hp_web_settings_url",webSettingsUrl);
}
}
