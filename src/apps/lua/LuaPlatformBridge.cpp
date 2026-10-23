#include "LuaPlatformBridge.h"
#include "LuaApp.h"
#include "LuaEvents.h"
#include "LuaJobService.h"
#include "../../services/StorageService.h"
#include "../../services/NetworkService.h"
#include "../../services/ImageResourceService.h"
#include "../../services/IdleService.h"
#include "../../services/LanguageService.h"
#include "../../ui/UiTheme.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {namespace {
void flag(lua_State* L,const char* name,bool value){lua_pushboolean(L,value);lua_setfield(L,-2,name);}
void number(lua_State* L,const char* name,uint32_t value){lua_pushnumber(L,value);lua_setfield(L,-2,name);}
int capabilities(lua_State* L){auto* app=LuaApp::self(L);lua_newtable(L);number(L,"version",1);
    number(L,"app_engine",3);number(L,"manifest_schema",3);number(L,"declarative_ui",3);
    number(L,"declarative_ui_minor",1);flag(L,"keyed_diff",true);flag(L,"virtual_list",true);flag(L,"month_grid",true);
    lua_newtable(L);for(const char* api:{"jobs","events","json","json_budget","weather_widget","weather_multi","icon_buttons","chart_cursor","password_inputs","storage","images","wallpaper","idle","file_stream","http_stream","postal_lookup","region"})number(L,api,1);
    flag(L,"websocket",false);flag(L,"mqtt",false);number(L,"printer",1);number(L,"calendar",1);number(L,"language",1);lua_setfield(L,-2,"apis");
    lua_newtable(L);for(const char* format:{"jpeg","png","bmp","webp","gif_first_frame","tga"})flag(L,format,true);
    flag(L,"heic",false);lua_setfield(L,-2,"image_formats");
    lua_newtable(L);number(L,"jobs",LuaJobService::kSlots);number(L,"response_bytes",LuaJobService::kResponseBytes);
    number(L,"request_bytes",LuaJobService::kBodyBytes);number(L,"image_cache_bytes",ImageResourceService::kBudget);
    number(L,"image_handles",ImageResourceService::kSlots);number(L,"event_subscriptions",LuaEventFilter::capacity);
    number(L,"stream_chunk_bytes",8192);number(L,"stream_sessions",3);number(L,"stream_file_sd_bytes",16*1024*1024);
    number(L,"stream_file_internal_bytes",1024*1024);lua_setfield(L,-2,"limits");
    lua_newtable(L);struct Permission {const char* name;AppPermission bit;};
    for(auto p:{Permission{"network.local",AppPermission::Network},{"network.internet",AppPermission::NetworkInternet},
        {"storage.read",AppPermission::StorageRead},{"storage.writeAppData",AppPermission::UserStorage},{"storage.nasRead",AppPermission::NasRead},
        {"calendar.read",AppPermission::CalendarRead},{"calendar.write",AppPermission::CalendarWrite},
        {"camera",AppPermission::Camera},{"microphone",AppPermission::Microphone},{"intercom",AppPermission::Intercom},{"door.open",AppPermission::DoorOpen},
        {"system.wallpaper",AppPermission::Wallpaper},{"power.idleIntegration",AppPermission::IdleIntegration},{"secrets",AppPermission::Secrets}})
        flag(L,p.name,app&&app->allows(p.bit));lua_setfield(L,-2,"permissions");return 1;
}
int snapshot(lua_State* L){lua_newtable(L);flag(L,"network_online",NetworkService::instance().online());
    flag(L,"sd_available",StorageService::instance().sdAvailable());flag(L,"internal_available",StorageService::instance().internalReady());
    number(L,"theme_generation",UiTheme::instance().generation());number(L,"idle_stage",static_cast<uint8_t>(IdleService::instance().stage()));
    number(L,"events_dropped",droppedLuaEvents());number(L,"uptime_ms",millis());return 1;
}
int appMemory(lua_State* L){auto* app=LuaApp::self(L);lua_newtable(L);if(!app)return 1;
    number(L,"lua_used",app->memoryUsed());number(L,"lua_peak",app->memoryPeak());number(L,"lua_limit",1024*1024);
    number(L,"native_used",app->nativeBytesUsed());number(L,"native_peak",app->nativeBytesPeak());number(L,"native_limit",512*1024);
    number(L,"ui_objects",app->uiObjectsUsed());number(L,"ui_objects_peak",app->uiObjectsPeak());number(L,"ui_objects_limit",1024);number(L,"budget_failures",app->budgetFailures());return 1;
}
int subscriptions(lua_State* L){auto* app=LuaApp::self(L);if(!app)return luaL_error(L,"app state missing");
    if(lua_isnil(L,1)){app->eventFilter().reset();lua_pushboolean(L,true);return 1;}luaL_checktype(L,1,LUA_TTABLE);
    if(lua_rawlen(L,1)>LuaEventFilter::capacity)return luaL_error(L,"subscription limit exceeded");
    LuaEventFilter filter;filter.begin();for(size_t i=1;i<=lua_rawlen(L,1);++i){lua_rawgeti(L,1,i);size_t length;const char* name=luaL_checklstring(L,-1,&length);
        if(strlen(name)!=length||!length||length>=48)return luaL_error(L,"invalid event name");
        for(size_t j=0;j<length;++j)if(!isalnum(static_cast<unsigned char>(name[j]))&&name[j]!='.'&&name[j]!='_')return luaL_error(L,"invalid event name");
        if(!filter.add(name))return luaL_error(L,"subscription limit exceeded");lua_pop(L,1);}
    app->eventFilter()=filter;lua_pushboolean(L,true);return 1;
}
int eventStatus(lua_State* L){auto* app=LuaApp::self(L);lua_newtable(L);number(L,"dropped",droppedLuaEvents());number(L,"capacity",16);
    number(L,"subscriptions",app?app->eventFilter().count():0);flag(L,"filtered",app&&app->eventFilter().explicitSelection());return 1;}
int language(lua_State* L){lua_pushstring(L,LanguageService::instance().active());return 1;}
int translate(lua_State* L){size_t n=0,size=0;const char* key=luaL_checklstring(L,1,&n);const char* fallback=luaL_optlstring(L,2,key,&size);
    if(n>96||strlen(key)!=n||size>2048||strlen(fallback)!=size)return luaL_error(L,"translation argument limit");lua_pushstring(L,tr(key,fallback));return 1;}
}
void registerLuaPlatformBridge(lua_State* L){lua_register(L,"hp_capabilities",capabilities);lua_register(L,"hp_system_snapshot",snapshot);
    lua_register(L,"hp_app_memory",appMemory);
    lua_register(L,"hp_events_subscribe",subscriptions);lua_register(L,"hp_events_status",eventStatus);lua_register(L,"hp_language",language);lua_register(L,"hp_tr",translate);}
}
