#include "LuaTimeBridge.h"
#include "../../services/TimeService.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
int epoch(lua_State* L){lua_pushinteger(L,TimeService::instance().nowUtc());return 1;}
void field(lua_State* L,const char* name,int value){lua_pushinteger(L,value);lua_setfield(L,-2,name);}
int local(lua_State* L){
    tm now{};auto& time=TimeService::instance();
    if(!time.localNow(now)){lua_pushnil(L);return 1;}
    lua_newtable(L);field(L,"year",now.tm_year+1900);field(L,"month",now.tm_mon+1);
    field(L,"day",now.tm_mday);field(L,"hour",now.tm_hour);field(L,"minute",now.tm_min);
    field(L,"second",now.tm_sec);field(L,"weekday",now.tm_wday?now.tm_wday:7);
    lua_pushinteger(L,time.nowUtc());lua_setfield(L,-2,"epoch");return 1;
}
int timezone(lua_State* L){const char* tz=getenv("TZ");lua_pushstring(L,tz?tz:"");return 1;}
int format(lua_State* L){
    const time_t value=static_cast<time_t>(luaL_checkinteger(L,1));
    size_t size;const char* pattern=luaL_checklstring(L,2,&size);
    if(size>96){lua_pushnil(L);return 1;}
    const String text=TimeService::instance().formatLocal(value,pattern);
    lua_pushlstring(L,text.c_str(),text.length());return 1;
}
}
void registerLuaTimeBridge(lua_State* L){
    lua_register(L,"hp_time_epoch",epoch);lua_register(L,"hp_time_local",local);
    lua_register(L,"hp_timezone",timezone);lua_register(L,"hp_format_time",format);
}
}
