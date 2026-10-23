#include "LuaBackgroundRun.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <cstring>
#include <initializer_list>
namespace hp {namespace {
int contextKey;
struct Budget {LuaBackgroundClock clock;uint32_t start;int remaining=kLuaBackgroundInstructions;bool exhausted=false;};
Budget* context(lua_State* L){lua_rawgetp(L,LUA_REGISTRYINDEX,&contextKey);auto* b=static_cast<Budget*>(lua_touserdata(L,-1));lua_pop(L,1);return b;}
void check(lua_State* L,Budget* b){if(!b)return;if(b->exhausted||uint32_t(b->clock()-b->start)>=kLuaBackgroundUs){b->exhausted=true;luaL_error(L,"background budget exceeded");}}
void hook(lua_State* L,lua_Debug*){auto* b=context(L);if(!b)return;b->remaining-=100;if(b->remaining<=0)b->exhausted=true;check(L,b);}
bool allowed(const char* name){
    const char* safe[]={"hp_millis","hp_language","hp_tr","hp_background_capabilities","hp_background_request",
        "hp_http_get","hp_http_post","hp_http_put","hp_http_delete","hp_http_download","hp_http_upload","hp_postal_lookup",
        "hp_stream_begin","hp_stream_write","hp_stream_commit","hp_stream_abort","hp_stream_read",
        "hp_job_status","hp_job_result","hp_job_cancel","hp_job_release","hp_capabilities","hp_system_snapshot","hp_events_status","hp_events_subscribe","hp_file_read","hp_file_write","hp_file_append","hp_file_exists","hp_file_delete","hp_file_size",
        "hp_kv_set_string","hp_kv_set_int","hp_kv_set_bool","hp_kv_set_float","hp_kv_delete","hp_secret_set","hp_secret_delete",
        "hp_storage_providers","hp_storage_list","hp_storage_stat","hp_storage_read","hp_storage_read_range",
        "hp_image_load_async","hp_image_thumbnail_async","hp_image_cancel","hp_image_release",
        "hp_printer_snapshot","hp_printer_files","hp_printer_macros","hp_printer_console","hp_printer_capabilities",
        "hp_json_decode","hp_json_encode"};
    for(const char* s:safe)if(!strcmp(s,name))return true;return false;
}
// The original can be a C closure (e.g. guarded UI call), not just a pointer.
int continuedFence(lua_State* L,int,lua_KContext){check(L,context(L));return lua_gettop(L);}
int closureFence(lua_State* L){auto* b=context(L);check(L,b);
    if(b&&!lua_toboolean(L,lua_upvalueindex(2)))return luaL_error(L,"operation unavailable during background execution");
    const int count=lua_gettop(L);lua_pushvalue(L,lua_upvalueindex(1));lua_insert(L,1);lua_callk(L,count,LUA_MULTRET,0,continuedFence);return continuedFence(L,LUA_OK,0);
}
void wrap(lua_State* L,const char* name,bool safe){lua_getfield(L,-1,name);if(!lua_iscfunction(L,-1)){lua_pop(L,1);return;}
    lua_pushboolean(L,safe);lua_pushcclosure(L,closureFence,2);lua_setfield(L,-2,name);
}
int deferredFunction(lua_State* L){if(context(L))return luaL_error(L,"operation unavailable during background execution");
    const int count=lua_gettop(L);lua_pushvalue(L,lua_upvalueindex(1));lua_insert(L,1);lua_call(L,count,1);
    lua_pushboolean(L,false);lua_pushcclosure(L,closureFence,2);return 1;
}
int invoke(lua_State* L){const char* event=static_cast<const char*>(lua_touserdata(L,1));const uint32_t job=lua_tointeger(L,2);
    lua_pushvalue(L,3);lua_rawsetp(L,LUA_REGISTRYINDEX,&contextKey);lua_sethook(L,hook,LUA_MASKCOUNT,100);
    lua_getglobal(L,event?"on_event":"on_background");if(!lua_isfunction(L,-1)){lua_pop(L,1);return 0;}
    if(event){lua_pushstring(L,event);lua_newtable(L);lua_pushinteger(L,job);lua_setfield(L,-2,"job");lua_call(L,2,0);}else lua_call(L,0,0);return 0;
}
}
bool luaBackgroundActive(lua_State* L){return context(L)!=nullptr;}
void protectLuaBackgroundCalls(lua_State* L){
    lua_pushglobaltable(L);lua_pushnil(L);
    while(lua_next(L,-2)){if(lua_type(L,-2)==LUA_TSTRING&&lua_iscfunction(L,-1)){
        const char* name=lua_tostring(L,-2);const bool hp=!strncmp(name,"hp_",3);
        // pcall rethrows latched budget errors, even when cached. xpcall's
        // arbitrary error handler can run while hooks are disabled; disallow
        // it in background instead of leaving an unbounded escape route.
        if(hp||!strcmp(name,"pcall")||!strcmp(name,"xpcall")||!strcmp(name,"print")||!strcmp(name,"collectgarbage")||!strcmp(name,"load")){
            const bool safe=hp?allowed(name):!strcmp(name,"pcall");
            lua_pushvalue(L,-2);lua_pushvalue(L,-2);lua_pushboolean(L,safe);lua_pushcclosure(L,closureFence,2);lua_rawset(L,-5);
        }
    }lua_pop(L,1);}lua_pop(L,1);
    lua_getglobal(L,"coroutine");if(lua_istable(L,-1)){wrap(L,"resume",false);wrap(L,"create",false);
        lua_getfield(L,-1,"wrap");lua_pushcclosure(L,deferredFunction,1);lua_setfield(L,-2,"wrap");}lua_pop(L,1);
    // Pattern matching and bulk string/table operations can spend unbounded
    // time inside a native C function without executing Lua instructions.
    lua_getglobal(L,"string");if(lua_istable(L,-1)){for(const char* name:{"find","match","gsub","rep","format","dump"})wrap(L,name,false);
        lua_getfield(L,-1,"gmatch");lua_pushcclosure(L,deferredFunction,1);lua_setfield(L,-2,"gmatch");}lua_pop(L,1);
    lua_getglobal(L,"table");if(lua_istable(L,-1)){for(const char* name:{"sort","concat","move","unpack"})wrap(L,name,false);}lua_pop(L,1);
}
bool runLuaBackground(lua_State* L,LuaBackgroundClock clock,const char* event,uint32_t job,uint32_t* elapsed){
    if(!L||!clock||luaBackgroundActive(L)||!lua_checkstack(L,8))return false;
    const int top=lua_gettop(L);Budget b{clock,clock()};const auto oldHook=lua_gethook(L);const int mask=lua_gethookmask(L),count=lua_gethookcount(L);
    // Registry growth and callback lookup happen inside pcall as well: a nearly
    // full app allocator must end this run, not panic the firmware.
    lua_pushcfunction(L,invoke);lua_pushlightuserdata(L,const_cast<char*>(event));lua_pushinteger(L,job);lua_pushlightuserdata(L,&b);
    const int result=lua_pcall(L,3,0,0);const uint32_t used=clock()-b.start;
    lua_sethook(L,oldHook,mask,count);lua_pushnil(L);lua_rawsetp(L,LUA_REGISTRYINDEX,&contextKey);lua_settop(L,top);
    if(elapsed)*elapsed=used;return result==LUA_OK&&!b.exhausted&&used<kLuaBackgroundUs;
}
}
