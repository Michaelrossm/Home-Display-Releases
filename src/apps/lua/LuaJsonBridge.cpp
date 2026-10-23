#include "LuaJsonBridge.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <string>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
constexpr size_t kBytes = 32768;
constexpr size_t kElements = 512;
constexpr unsigned kDepth = 12;
char nullValue;
// ArduinoJson's document uses PSRAM and cannot allocate beyond its quota.
class JsonAllocator final : public ArduinoJson::Allocator {
    struct Header { size_t size; };
    size_t used_ = 0;
public:
    void* allocate(size_t bytes) override {
        if (bytes > 65536 - used_) return nullptr;
        auto* h = static_cast<Header*>(heap_caps_malloc(sizeof(Header)+bytes, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!h) return nullptr;
        h->size=bytes; used_+=bytes; return h+1;
    }
    void deallocate(void* ptr) override {
        if (!ptr) return;
        auto* h=static_cast<Header*>(ptr)-1; used_-=h->size; heap_caps_free(h);
    }
    void* reallocate(void* ptr,size_t bytes) override {
        if (!ptr) return allocate(bytes);
        auto* h=static_cast<Header*>(ptr)-1;
        if (bytes > 65536-(used_-h->size)) return nullptr;
        const size_t old=h->size;
        auto* next=static_cast<Header*>(heap_caps_realloc(h,sizeof(Header)+bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!next) return nullptr;
        used_=used_-old+bytes; next->size=bytes; return next+1;
    }
};
bool pushJson(lua_State* L,JsonVariantConst value,unsigned depth,size_t& count,size_t limit=kElements) {
    if (depth>kDepth || ++count>limit) return false;
    if (value.is<JsonArrayConst>()) {
        lua_newtable(L); int index=1;
        for (auto item:value.as<JsonArrayConst>()) {
            if (!pushJson(L,item,depth+1,count,limit)) return false;
            lua_rawseti(L,-2,index++);
        }
    } else if (value.is<JsonObjectConst>()) {
        lua_newtable(L);
        for (auto item:value.as<JsonObjectConst>()) {
            if (strlen(item.key().c_str())>1024 || !pushJson(L,item.value(),depth+1,count,limit)) return false;
            lua_setfield(L,-2,item.key().c_str());
        }
    } else if (value.is<const char*>()) {
        auto s=value.as<JsonString>(); if(s.size()>8192)return false;
        lua_pushlstring(L,s.c_str(),s.size());
    } else if(value.is<bool>()) lua_pushboolean(L,value.as<bool>());
    else if(value.is<lua_Integer>()) lua_pushinteger(L,value.as<lua_Integer>());
    else if(value.is<double>()) lua_pushnumber(L,value.as<double>());
    else lua_pushlightuserdata(L,&nullValue);
    return true;
}
bool readLua(lua_State* L,int index,JsonVariant out,unsigned depth,size_t& count,size_t limit=kElements) {
    if(depth>kDepth || ++count>limit)return false;
    index=lua_absindex(L,index);
    switch(lua_type(L,index)) {
        case LUA_TNIL: out.set(nullptr); return true;
        case LUA_TLIGHTUSERDATA: if(lua_touserdata(L,index)==&nullValue){out.set(nullptr);return true;}return false;
        case LUA_TBOOLEAN: out.set(lua_toboolean(L,index)!=0); return true;
        case LUA_TNUMBER: {
            if(lua_isinteger(L,index)){out.set(lua_tointeger(L,index));return true;}
            const double n=lua_tonumber(L,index); if(!std::isfinite(n))return false;
            out.set(n);return true;
        }
        case LUA_TSTRING: {
            size_t size;const char* s=lua_tolstring(L,index,&size);
            if(size>8192)return false;
            out.set(std::string(s,size));return true;
        }
        case LUA_TTABLE: {
            const size_t length=lua_rawlen(L,index);
            if(length>limit)return false;
            if(length) {
                auto array=out.to<JsonArray>();
                for(size_t i=1;i<=length;++i) {
                    lua_rawgeti(L,index,i);const bool ok=readLua(L,-1,array.add<JsonVariant>(),depth+1,count,limit);lua_pop(L,1);
                    if(!ok)return false;
                }
                // Reject sparse/mixed arrays rather than silently discard keys.
                size_t keys=0;lua_pushnil(L);
                while(lua_next(L,index)) { ++keys;bool ok=lua_isinteger(L,-2)&&lua_tointeger(L,-2)>0&&static_cast<size_t>(lua_tointeger(L,-2))<=length;lua_pop(L,1);if(!ok){lua_pop(L,1);return false;} }
                return keys==length;
            }
            auto object=out.to<JsonObject>();lua_pushnil(L);
            while(lua_next(L,index)) {
                if(lua_type(L,-2)!=LUA_TSTRING){lua_pop(L,2);return false;}
                size_t size;const char* key=lua_tolstring(L,-2,&size);
                if(size>1024||!readLua(L,-1,object[key].to<JsonVariant>(),depth+1,count,limit)){lua_pop(L,2);return false;}
                lua_pop(L,1);
            }
            return true;
        }
        default:return false;
    }
}
struct DecodeContext { JsonDocument* document; bool valid=false;size_t limit=kElements; };
int decodeTable(lua_State* L) {
    auto* context=static_cast<DecodeContext*>(lua_touserdata(L,1));size_t count=0;
    context->valid=pushJson(L,context->document->as<JsonVariantConst>(),0,count,context->limit);
    if(!context->valid){lua_settop(L,1);lua_pushnil(L);}
    return 1;
}
int decodeInput(lua_State* L,const char* input,size_t size,size_t limit=kElements) {
    if(size>kBytes){lua_pushnil(L);lua_pushliteral(L,"json_size");return 2;}
    // Catch Lua OOM inside table conversion: longjmp must not bypass the
    // ArduinoJson/PSRAM allocator destructors in this outer C++ frame.
    if(!lua_checkstack(L,128)){lua_pushnil(L);return 1;}
    const int top=lua_gettop(L);
    lua_pushliteral(L,"json_invalid_or_limit");lua_pushcfunction(L,decodeTable);
    JsonAllocator allocator;JsonDocument doc(&allocator);
    auto error=deserializeJson(doc,input,size,DeserializationOption::NestingLimit(kDepth));
    DecodeContext context{&doc,false,limit};int rc=LUA_ERRRUN;
    if(!error){lua_pushlightuserdata(L,&context);rc=lua_pcall(L,1,1,0);}
    if(error||rc!=LUA_OK||!context.valid) {
        lua_settop(L,top+1);lua_pushnil(L);lua_pushvalue(L,top+1);return 2;
    }
    return 1;
}
struct EncodeContext { const char* text; size_t size; };
size_t elementBudget(lua_State* L){
    if(lua_isnoneornil(L,2))return kElements;
    luaL_checktype(L,2,LUA_TTABLE);lua_getfield(L,2,"max_elements");
    const auto limit=luaL_optinteger(L,-1,kElements);lua_pop(L,1);
    luaL_argcheck(L,limit>=1&&limit<=2048,2,"max_elements must be 1..2048");return size_t(limit);
}
int decode(lua_State* L){size_t size;const char* input=luaL_checklstring(L,1,&size);const auto limit=elementBudget(L);return decodeInput(L,input,size,limit);}
int encodeString(lua_State* L){auto* context=static_cast<EncodeContext*>(lua_touserdata(L,1));lua_pushlstring(L,context->text,context->size);return 1;}
int encode(lua_State* L) {
    luaL_checktype(L,1,LUA_TTABLE);
    const auto limit=elementBudget(L);
    if(!lua_checkstack(L,128)){lua_pushnil(L);return 1;}
    const int top=lua_gettop(L);lua_pushliteral(L,"json_invalid_or_limit");lua_pushcfunction(L,encodeString);
    JsonAllocator allocator;JsonDocument doc(&allocator);size_t count=0;
    if(!readLua(L,1,doc.to<JsonVariant>(),0,count,limit)||doc.overflowed()||measureJson(doc)>kBytes) {
        lua_settop(L,top+1);lua_pushnil(L);lua_pushvalue(L,top+1);return 2;
    }
    std::string output;serializeJson(doc,output);EncodeContext context{output.c_str(),output.length()};
    lua_pushlightuserdata(L,&context);
    if(lua_pcall(L,1,1,0)!=LUA_OK){lua_settop(L,top+1);lua_pushnil(L);lua_pushvalue(L,top+1);return 2;}
    return 1;
}
}
namespace {
struct NativeJsonInput{const char* text;size_t size,limit=kElements;};
int decodeNative(lua_State* L){auto* input=static_cast<NativeJsonInput*>(lua_touserdata(L,1));return decodeInput(L,input->text,input->size,input->limit);}
}
int pushLuaJson(lua_State* L,const char* text,size_t size){
    return pushLuaJsonBounded(L,text,size,kElements);
}
int pushLuaJsonBounded(lua_State* L,const char* text,size_t size,size_t maxElements){
    if(maxElements>2048||!maxElements){lua_pushnil(L);return 1;}
    if(!lua_checkstack(L,8)){lua_pushnil(L);return 1;}
    const int top=lua_gettop(L);NativeJsonInput input{text,size,maxElements};
    lua_pushcfunction(L,decodeNative);lua_pushlightuserdata(L,&input);
    if(lua_pcall(L,1,LUA_MULTRET,0)!=LUA_OK){lua_settop(L,top);lua_pushnil(L);return 1;}
    return lua_gettop(L)-top;
}
bool encodeLuaJson(lua_State* L,int index,String& output){
    if(!lua_checkstack(L,8))return false;
    index=lua_absindex(L,index);const int top=lua_gettop(L);
    lua_pushcfunction(L,encode);lua_pushvalue(L,index);
    const int rc=lua_pcall(L,1,1,0);bool valid=false;
    if(rc==LUA_OK&&lua_type(L,-1)==LUA_TSTRING){size_t size;const char* raw=lua_tolstring(L,-1,&size);output=String(raw,size);valid=output.length()==size;}
    lua_settop(L,top);return valid;
}
void registerLuaJsonBridge(lua_State* L) {
    lua_register(L,"hp_json_decode",decode);lua_register(L,"hp_json_encode",encode);
    lua_pushlightuserdata(L,&nullValue);lua_setglobal(L,"hp_json_null");
}
}
