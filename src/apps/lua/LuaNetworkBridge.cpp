#include "LuaNetworkBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "LuaJobError.h"
#include "../../services/PostalLookupJob.h"
#include "../../services/AppFileStreamService.h"
#include "../../services/NetworkService.h"
#include "../../services/RegionService.h"
#include <ArduinoJson.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
bool option(lua_State* L,const char* name,String& value,size_t limit) {
    lua_getfield(L,1,name);
    if(lua_isnil(L,-1)){lua_pop(L,1);return true;}
    if(lua_type(L,-1)!=LUA_TSTRING){lua_pop(L,1);return false;}
    size_t length;const char* text=lua_tolstring(L,-1,&length);
    const bool valid=length<=limit&&strlen(text)==length;
    if(valid)value=String(text,length);lua_pop(L,1);return valid;
}
int request(lua_State* L,uint8_t method,uint8_t streamMode=0) {
    luaL_checktype(L,1,LUA_TTABLE);auto* app=LuaApp::self(L);
    if(!app){lua_pushnil(L);return 1;}
    LuaJobService::Request r;r.owner=app->id();r.permissions=app->permissions();r.method=method;
    r.streamMode=streamMode;
    lua_getfield(L,1,"coalesce");
    if(!lua_isnil(L,-1)&&!lua_isboolean(L,-1))return luaL_error(L,"coalesce must be boolean");
    r.coalesce=lua_toboolean(L,-1)&&method==0&&!streamMode;lua_pop(L,1);
    bool valid=option(L,"url",r.url,1024)&&option(L,"body",r.body,LuaJobService::kBodyBytes)&&
        option(L,"content_type",r.contentType,128)&&option(L,"bearer_secret",r.bearerSecret,48)&&option(L,"basic_auth_secret",r.basicSecret,48);
    lua_getfield(L,1,"timeout");if(!lua_isnil(L,-1))r.timeout=LV_CLAMP(250,lua_tointeger(L,-1),5000);lua_pop(L,1);
    if(streamMode){valid=valid&&app->allows(AppPermission::UserStorage)&&option(L,"file",r.fileName,96)&&
        AppFileStreamService::validName(r.fileName)&&!r.body.length();
        lua_getfield(L,1,"total_timeout");if(!lua_isnil(L,-1))r.deadlineMs=LV_CLAMP(1000,lua_tointeger(L,-1),120000);lua_pop(L,1);}
    if(r.bearerSecret.length()&&!app->allows(AppPermission::Secrets))valid=false;
    if(r.basicSecret.length()&&!app->allows(AppPermission::Secrets))valid=false;
    if(r.bearerSecret.length()&&r.basicSecret.length())valid=false;
    for(size_t i=0;i<r.url.length();++i)if(static_cast<unsigned char>(r.url[i])<=32)valid=false;
    if(r.contentType.indexOf('\r')>=0||r.contentType.indexOf('\n')>=0)valid=false;
    lua_getfield(L,1,"headers");
    if(lua_istable(L,-1)) {
        lua_pushnil(L);
        while(lua_next(L,-2)) {
            if(r.headerCount>=8||lua_type(L,-2)!=LUA_TSTRING||lua_type(L,-1)!=LUA_TSTRING){valid=false;lua_pop(L,2);break;}
            size_t ns,vs;const char* n=lua_tolstring(L,-2,&ns);const char* v=lua_tolstring(L,-1,&vs);
            if(ns>64||vs>512||strlen(n)!=ns||strlen(v)!=vs){valid=false;lua_pop(L,2);break;}
            String name(n,ns),value(v,vs),lower=name;lower.toLowerCase();
            if(!ns||lower=="host"||lower=="content-length"||lower=="transfer-encoding"||lower=="authorization")valid=false;
            for(size_t i=0;i<ns;++i)if(!isalnum(static_cast<unsigned char>(n[i]))&&n[i]!='-')valid=false;
            if(value.indexOf('\r')>=0||value.indexOf('\n')>=0)valid=false;
            r.headers[r.headerCount++]={name,value};lua_pop(L,1);
        }
    } else if(!lua_isnil(L,-1))valid=false;
    lua_pop(L,1);
    const uint32_t id=valid?LuaJobService::instance().submit(r):0;
    if(id)lua_pushinteger(L,id);else lua_pushnil(L);
    return 1;
}
int region(lua_State* L){auto& service=RegionService::instance();lua_newtable(L);lua_pushinteger(L,1);lua_setfield(L,-2,"version");lua_pushstring(L,service.country());lua_setfield(L,-2,"country");lua_newtable(L);
    for(size_t i=0;i<RegionService::count();++i){lua_newtable(L);lua_pushstring(L,RegionService::code(i));lua_setfield(L,-2,"code");lua_pushstring(L,RegionService::name(i));lua_setfield(L,-2,"name");lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"countries");return 1;}
int get(lua_State* L){return request(L,0);}int post(lua_State* L){return request(L,1);}
int postalLookup(lua_State* L){
    luaL_checktype(L,1,LUA_TTABLE);auto* app=LuaApp::self(L);
    if(!app||!app->allows(AppPermission::NetworkInternet)){lua_pushnil(L);return 1;}
    String postal,country;
    if(!option(L,"postal_code",postal,16)||!postal.length()||!option(L,"country",country,32)){lua_pushnil(L);return 1;}
    LuaJobService::Request r;r.owner=app->id();r.permissions=app->permissions();r.required=perm(AppPermission::NetworkInternet);
    r.kind=5;r.handler=executePostalLookupJob;r.completion="http.completed";r.lane=LuaJobService::Request::Lane::Transfer;
    JsonDocument args;args["postal_code"]=postal;args["country"]=country;serializeJson(args,r.body);
    const auto id=LuaJobService::instance().submit(r);if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int put(lua_State* L){return request(L,2);}int remove(lua_State* L){return request(L,3);}
int download(lua_State* L){return request(L,0,1);}int upload(lua_State* L){return request(L,2,2);}
int pushResult(lua_State* L) {
    const auto& r=*static_cast<LuaJobService::Result*>(lua_touserdata(L,1));
    lua_newtable(L);
    lua_pushboolean(L,r.done);lua_setfield(L,-2,"done");lua_pushboolean(L,r.ok);lua_setfield(L,-2,"ok");
    lua_pushinteger(L,r.status);lua_setfield(L,-2,"status");
    lua_pushinteger(L,r.size);lua_setfield(L,-2,"size");
    lua_pushlstring(L,r.body.c_str(),r.body.length());lua_setfield(L,-2,"body");
    lua_pushstring(L,r.error.c_str());lua_setfield(L,-2,"error");
    lua_pushstring(L,luaJobErrorCode(r.error.c_str()));lua_setfield(L,-2,"error_code");
    lua_pushboolean(L,r.cancelled);lua_setfield(L,-2,"cancelled");lua_pushboolean(L,r.retryable);lua_setfield(L,-2,"retryable");
    lua_pushboolean(L,r.coalesced);lua_setfield(L,-2,"coalesced");
    lua_pushstring(L,!r.done?(r.cancelled?"cancelling":r.startedAt?"running":"queued"):r.cancelled?"cancelled":r.ok?"succeeded":"failed");lua_setfield(L,-2,"state");
    lua_pushnumber(L,r.processed);lua_setfield(L,-2,"processed");lua_pushnumber(L,r.total);lua_setfield(L,-2,"total");
    lua_pushinteger(L,r.startedAt?uint32_t(r.startedAt-r.queuedAt):0);lua_setfield(L,-2,"queue_ms");
    lua_pushinteger(L,r.startedAt?uint32_t((r.done?r.finishedAt:millis())-r.startedAt):0);lua_setfield(L,-2,"run_ms");return 1;
}
int result(lua_State* L) {
    const uint32_t id=luaL_checkinteger(L,1);auto* app=LuaApp::self(L);
    lua_pushcfunction(L,pushResult);LuaJobService::Result r;
    if(!app||!LuaJobService::instance().result(app->id(),id,r)){lua_pop(L,1);lua_pushnil(L);return 1;}
    lua_pushlightuserdata(L,&r);
    if(lua_pcall(L,1,1,0)!=LUA_OK){lua_pop(L,1);lua_pushnil(L);}
    return 1; // Result's C++ buffers are released even if Lua table allocation fails.
}
int status(lua_State* L) {
    auto* app=LuaApp::self(L);LuaJobService::Result r;
    if(!app||!LuaJobService::instance().result(app->id(),luaL_checkinteger(L,1),r)){lua_pushnil(L);return 1;}
    lua_pushstring(L,r.done?"done":"pending");return 1;
}
int cancel(lua_State* L) {
    auto* app=LuaApp::self(L);lua_pushboolean(L,app&&LuaJobService::instance().cancel(app->id(),luaL_checkinteger(L,1)));return 1;
}
int release(lua_State* L){auto* app=LuaApp::self(L);lua_pushboolean(L,app&&LuaJobService::instance().release(app->id(),luaL_checkinteger(L,1)));return 1;}
int network(lua_State* L) {
    auto& n=NetworkService::instance();lua_newtable(L);
    lua_pushboolean(L,n.online());lua_setfield(L,-2,"online");
    lua_pushboolean(L,n.usingEthernet());lua_setfield(L,-2,"lan");
    lua_pushboolean(L,n.usingWifi());lua_setfield(L,-2,"wifi");
    lua_pushstring(L,n.ip().c_str());lua_setfield(L,-2,"ip");
    // Link connectivity is not proof of WAN connectivity: unknown, not guessed.
    return 1;
}
}
void registerLuaNetworkBridge(lua_State* L) {
    lua_register(L,"hp_network_status",network);
    lua_register(L,"hp_region",region);
    auto* app=LuaApp::self(L);
    if(!app)return;
    if(app->allows(AppPermission::NetworkInternet))lua_register(L,"hp_postal_lookup",postalLookup);
    if(app->allows(AppPermission::Network)||app->allows(AppPermission::NetworkInternet)) {
    lua_register(L,"hp_http_get",get);lua_register(L,"hp_http_post",post);
    lua_register(L,"hp_http_put",put);lua_register(L,"hp_http_delete",remove);
    if(app->allows(AppPermission::UserStorage)){lua_register(L,"hp_http_download",download);lua_register(L,"hp_http_upload",upload);}
    }
    lua_register(L,"hp_job_status",status);lua_register(L,"hp_job_result",result);lua_register(L,"hp_job_cancel",cancel);
    lua_register(L,"hp_job_release",release);
}
}
