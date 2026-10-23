#include "LuaDiscoveryBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "../../services/MinimalMdns.h"
#include <ArduinoJson.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
int start(lua_State* L) {
    luaL_checktype(L,1,LUA_TTABLE);auto* app=LuaApp::self(L);
    const size_t count=lua_rawlen(L,1);String services;bool valid=count>0&&count<=4;
    for(size_t i=1;valid&&i<=count;++i) {
        lua_rawgeti(L,1,i);size_t length=0;const char* raw=lua_tolstring(L,-1,&length);
        if(!raw||length>80||strlen(raw)!=length){lua_pop(L,1);valid=false;break;}
        String name(raw,length);lua_pop(L,1);
        const int separator=name.indexOf("._tcp.local");
        if(!name.startsWith("_")||separator<2||separator+11!=static_cast<int>(name.length())){valid=false;break;}
        for(int j=1;j<separator;++j)if(!isalnum(static_cast<unsigned char>(name[j]))&&name[j]!='-')valid=false;
        if(i>1)services+='\n';services+=name;
    }
    LuaJobService::Request request;request.kind=2;request.body=services;
    if(app){request.owner=app->id();request.permissions=app->permissions();}
    const uint32_t id=valid&&app?LuaJobService::instance().submit(request):0;
    if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int pushResults(lua_State* L) {
    const auto& doc=*static_cast<JsonDocument*>(lua_touserdata(L,1));
    lua_newtable(L);int i=1;
    for(JsonObjectConst item:doc.as<JsonArrayConst>()) {
        lua_newtable(L);
        for(const char* key:{"service","hostname","host"}){lua_pushstring(L,item[key].as<const char*>());lua_setfield(L,-2,key);}
        lua_pushinteger(L,item["port"].as<int>());lua_setfield(L,-2,"port");lua_rawseti(L,-2,i++);
    }return 1;
}
int results(lua_State* L) {
    const uint32_t id=luaL_checkinteger(L,1);auto* app=LuaApp::self(L);
    lua_pushcfunction(L,pushResults);LuaJobService::Result result;
    if(!app||!LuaJobService::instance().result(app->id(),id,result)||!result.done||!result.ok){lua_pop(L,1);lua_pushnil(L);return 1;}
    JsonDocument doc;if(deserializeJson(doc,result.body)){lua_pop(L,1);lua_pushnil(L);return 1;}
    lua_pushlightuserdata(L,&doc);
    if(lua_pcall(L,1,1,0)!=LUA_OK){lua_pop(L,1);lua_pushnil(L);}
    return 1;
}
}
String executeLuaDiscovery(const String& input) {
    String services[4];size_t count=0;int offset=0;
    while(offset<static_cast<int>(input.length())&&count<4){int end=input.indexOf('\n',offset);if(end<0)end=input.length();services[count++]=input.substring(offset,end);offset=end+1;}
    MdnsDiscoveryResult results[8];const int size=queryMdnsServices(services,count,results,8);
    JsonDocument doc;auto array=doc.to<JsonArray>();
    for(int i=0;i<size;++i){auto o=array.add<JsonObject>();o["service"]=results[i].service;o["hostname"]=results[i].hostname;o["host"]=results[i].host;o["port"]=results[i].port;}
    String output;serializeJson(doc,output);return output;
}
void registerLuaDiscoveryBridge(lua_State* L) {
    auto* app=LuaApp::self(L);if(!app||!app->allows(AppPermission::Discovery)||!app->allows(AppPermission::Network))return;
    lua_register(L,"hp_discovery_start",start);lua_register(L,"hp_discovery_results",results);
}
}
