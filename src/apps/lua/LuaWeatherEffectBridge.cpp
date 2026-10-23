#include "LuaWeatherEffectBridge.h"
#include "LuaApp.h"
#include "../../ui/UiAmbientAnimationService.h"
#include <lvgl.h>
#include <algorithm>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace hp {
namespace {

bool ownedBy(LuaApp* app,lv_obj_t* object){
    if(!app||!object||!lv_obj_is_valid(object))return false;
    for(auto* p=object;p;p=lv_obj_get_parent(p))if(p==app->screenRoot())return true;
    return false;
}

UiAmbientAnimationService::WeatherConfig readConfig(
    lua_State* L,int index,UiAmbientAnimationService::WeatherConfig base,
    bool& typePresent,bool& intensityPresent,bool& sizePresent,bool& enabledPresent
){
    luaL_checktype(L,index,LUA_TTABLE);

    lua_getfield(L,index,"type");
    typePresent=lua_isstring(L,-1);
    if(typePresent)base.type=UiAmbientAnimationService::parseWeatherType(lua_tostring(L,-1));
    lua_pop(L,1);

    lua_getfield(L,index,"intensity");
    intensityPresent=lua_isnumber(L,-1);
    if(intensityPresent)base.intensity=static_cast<uint8_t>(
        std::max(0,std::min(100,static_cast<int>(lua_tointeger(L,-1))))
    );
    lua_pop(L,1);

    bool widthPresent=false,heightPresent=false;
    lua_getfield(L,index,"width");widthPresent=lua_isnumber(L,-1);
    if(widthPresent)base.width=static_cast<uint16_t>(
        std::max(32,std::min(160,static_cast<int>(lua_tointeger(L,-1))))
    );
    lua_pop(L,1);

    lua_getfield(L,index,"height");heightPresent=lua_isnumber(L,-1);
    if(heightPresent)base.height=static_cast<uint16_t>(
        std::max(28,std::min(120,static_cast<int>(lua_tointeger(L,-1))))
    );
    lua_pop(L,1);
    sizePresent=widthPresent||heightPresent;

    lua_getfield(L,index,"enabled");
    enabledPresent=!lua_isnil(L,-1);
    if(enabledPresent)base.enabled=lua_toboolean(L,-1);
    lua_pop(L,1);

    return base;
}

int createEffect(lua_State* L){
    auto* app=LuaApp::self(L);
    auto* parent=static_cast<lv_obj_t*>(lua_touserdata(L,1));
    if(!app||(app->lifecycle()!=LuaRuntimeState::Foreground&&app->lifecycle()!=LuaRuntimeState::Opening)||!ownedBy(app,parent))
        return luaL_error(L,"weather effect requires a foreground app-owned container");
    if(!lua_istable(L,2))return luaL_error(L,"weather effect options table required");

    auto& service=UiAmbientAnimationService::instance();

    bool tp=false,ip=false,sp=false,ep=false;
    UiAmbientAnimationService::WeatherConfig config;
    config=readConfig(L,2,config,tp,ip,sp,ep);

    auto* effect=service.createWeatherEffect(app,parent,config);
    if(!effect){lua_pushnil(L);return 1;}
    lua_pushlightuserdata(L,effect);return 1;
}

int setEffect(lua_State* L){
    auto* app=LuaApp::self(L);
    auto* handle=static_cast<lv_obj_t*>(lua_touserdata(L,1));
    // The UI pump constructs/configures widgets while Opening. The central
    // animator still refuses to run them until the app is actually Foreground.
    if(!app||(app->lifecycle()!=LuaRuntimeState::Opening&&!app->uiAnimationEligible()))
        return luaL_error(L,"weather effect update requires foreground UI");
    if(!handle||!ownedBy(app,handle))
        return luaL_error(L,"invalid weather effect");
    if(!lua_istable(L,2))return luaL_error(L,"weather effect options table required");

    UiAmbientAnimationService::WeatherConfig config;
    config.width=static_cast<uint16_t>(lv_obj_get_width(handle));
    config.height=static_cast<uint16_t>(lv_obj_get_height(handle));
    bool tp=false,ip=false,sp=false,ep=false;
    config=readConfig(L,2,config,tp,ip,sp,ep);

    const bool ok=UiAmbientAnimationService::instance().updateWeatherEffect(
        app,handle,config,tp,ip,sp,ep
    );
    lua_pushboolean(L,ok);return 1;
}

int weatherSymbol(lua_State* L){
    lua_pushstring(
        L,
        UiAmbientAnimationService::weatherSymbol(
            UiAmbientAnimationService::parseWeatherType(luaL_checkstring(L,1))
        )
    );
    return 1;
}

}

void registerLuaWeatherEffectBridge(lua_State* L){
    lua_register(L,"hp_ui_weather_effect",createEffect);
    lua_register(L,"hp_ui_weather_effect_set",setEffect);
    lua_register(L,"hp_weather_symbol",weatherSymbol);
}

}
