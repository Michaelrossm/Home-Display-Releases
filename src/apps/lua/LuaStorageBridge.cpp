#include "LuaStorageBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "../../services/ConfigService.h"
#include "../../services/StorageService.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <cmath>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
// NVS keys are at most 15 bytes. Store and verify the full identity as well
// as its digest, so even a digest collision cannot cross app namespaces.
String keyFor(const String& owner,const String& name,char prefix) {
    const String identity=owner+":"+name;unsigned char digest[32];
    if(mbedtls_sha256(reinterpret_cast<const unsigned char*>(identity.c_str()),identity.length(),digest,0))return "";
    char key[16];key[0]=prefix;
    for(unsigned i=0;i<7;++i)snprintf(key+1+i*2,3,"%02x",digest[i]);
    return key;
}
bool nameValid(const String& name) {
    if(!name.length()||name.length()>48)return false;
    for(size_t i=0;i<name.length();++i){char c=name[i];if(!isalnum(static_cast<unsigned char>(c))&&c!='_'&&c!='-')return false;}
    return true;
}
String stored(const String& owner,const String& name,bool secret) {
    auto& config=ConfigService::instance();const String key=keyFor(owner,name,secret?'s':'k');
    return secret?config.getSecretString(key.c_str()):config.getString(key.c_str());
}
bool identityMatches(const String& record,const String& owner,const String& name) {
    JsonDocument doc;if(deserializeJson(doc,record))return false;
    return doc["owner"].as<String>()==owner&&doc["name"].as<String>()==name;
}
String readValue(const String& owner,const String& name,bool secret,bool& exists) {
    const String record=stored(owner,name,secret);exists=identityMatches(record,owner,name);
    if(!exists)return "";
    JsonDocument doc;deserializeJson(doc,record);return doc["value"].as<String>();
}
bool fileNameValid(const String& name) {
    if(!name.length()||name.length()>96||name.startsWith("/")||name.indexOf("..")>=0||name.indexOf('\\')>=0||name.indexOf(':')>=0||name.indexOf("//")>=0)return false;
    unsigned slashes=0;
    for(size_t i=0;i<name.length();++i){const char c=name[i];if(c=='/')++slashes;else if(!isalnum(static_cast<unsigned char>(c))&&c!='_'&&c!='-'&&c!='.')return false;}
    return slashes<=3&&!name.endsWith("/")&&!name.endsWith(".tmp")&&!name.endsWith(".bak");
}
bool usage(fs::FS& fs,const String& path,size_t& bytes,unsigned& files,unsigned depth=0) {
    if(depth>4)return false;
    File directory=fs.open(path);if(!directory)return true;
    if(!directory.isDirectory()){bytes+=directory.size();++files;directory.close();return files<=32;}
    File item=directory.openNextFile();
    while(item){String child=item.path();bool folder=item.isDirectory();size_t size=item.size();item.close();
        if(folder){if(!usage(fs,child,bytes,files,depth+1)){directory.close();return false;}}
        else{bytes+=size;if(++files>32){directory.close();return false;}}
        item=directory.openNextFile();
    }directory.close();return true;
}
int queue(lua_State* L,uint8_t operation,bool secret=false) {
    auto* app=LuaApp::self(L);size_t nameSize;const char* raw=luaL_checklstring(L,1,&nameSize);
    String name(raw,nameSize),value;
    bool valid=strlen(raw)==nameSize&&(operation<10?nameValid(name):fileNameValid(name));
    if(operation==1||operation==4||operation==11||operation==12) {
        size_t size;const char* data=luaL_checklstring(L,2,&size);
        if(size>(secret?2048:8192))valid=false;else value=String(data,size);
    }
    LuaJobService::Request request;
    if(app){request.owner=app->id();request.permissions=app->permissions();}
    request.kind=1;request.method=operation;request.url=name;request.body=value;
    const uint32_t id=valid&&app?LuaJobService::instance().submit(request):0;
    if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int secretSet(lua_State* L){return queue(L,4,true);}int secretDelete(lua_State* L){return queue(L,5,true);}
int secretExists(lua_State* L) {
    auto* app=LuaApp::self(L);const String name=luaL_checkstring(L,1);bool found=false;
    if(app&&nameValid(name))readValue(app->id(),name,true,found);
    lua_pushboolean(L,found);return 1;
}
int fileRead(lua_State* L){return queue(L,10);}int fileWrite(lua_State* L){return queue(L,11);}
int fileAppend(lua_State* L){return queue(L,12);}int fileExists(lua_State* L){return queue(L,13);}
int fileDelete(lua_State* L){return queue(L,14);}int fileSize(lua_State* L){return queue(L,15);}
int kvGet(lua_State* L,int type) {
    auto* app=LuaApp::self(L);const String name=luaL_checkstring(L,1);bool found=false;
    String value=app&&nameValid(name)?readValue(app->id(),name,false,found):String();
    JsonDocument doc;if(found&&deserializeJson(doc,value))found=false;
    if(found&&doc["type"].as<int>()!=type)found=false;
    if(!found){if(lua_gettop(L)>=2)lua_pushvalue(L,2);else if(type==0)lua_pushliteral(L,"");else if(type==2)lua_pushboolean(L,false);else lua_pushnumber(L,0);return 1;}
    if(type==0)lua_pushstring(L,doc["data"].as<const char*>());
    else if(type==2)lua_pushboolean(L,doc["data"].as<bool>());
    else if(type==1)lua_pushinteger(L,doc["data"].as<int32_t>());
    else lua_pushnumber(L,doc["data"].as<double>());
    return 1;
}
int kvSet(lua_State* L,int type) {
    JsonDocument doc;doc["type"]=type;
    if(type==0){size_t size;const char* value=luaL_checklstring(L,2,&size);if(size>2048){lua_pushnil(L);return 1;}doc["data"]=String(value,size);}
    else if(type==2){luaL_checktype(L,2,LUA_TBOOLEAN);doc["data"]=lua_toboolean(L,2)!=0;}
    else if(type==1)doc["data"]=luaL_checkinteger(L,2);
    else {const double n=luaL_checknumber(L,2);if(!std::isfinite(n)){lua_pushnil(L);return 1;}doc["data"]=n;}
    String value;serializeJson(doc,value);lua_settop(L,1);lua_pushlstring(L,value.c_str(),value.length());return queue(L,1);
}
int stringGet(lua_State* L){return kvGet(L,0);}int stringSet(lua_State* L){return kvSet(L,0);}
int intGet(lua_State* L){return kvGet(L,1);}int intSet(lua_State* L){return kvSet(L,1);}
int boolGet(lua_State* L){return kvGet(L,2);}int boolSet(lua_State* L){return kvSet(L,2);}
int floatGet(lua_State* L){return kvGet(L,3);}int floatSet(lua_State* L){return kvSet(L,3);}
int kvExists(lua_State* L){auto* app=LuaApp::self(L);bool found=false;const String name=luaL_checkstring(L,1);if(app&&nameValid(name))readValue(app->id(),name,false,found);lua_pushboolean(L,found);return 1;}
int kvDelete(lua_State* L){return queue(L,2);}
}
String luaSecretForNative(const String& owner,const String& name){bool exists=false;return nameValid(name)?readValue(owner,name,true,exists):String();}
LuaStorageResult executeLuaStorage(const String& owner,uint8_t operation,const String& name,const String& value) {
    LuaStorageResult result;
    if(operation<10) {
        if(!nameValid(name)){result.error="storage_name";return result;}
        const bool secret=operation>=4;auto& config=ConfigService::instance();
        const String key=keyFor(owner,name,secret?'s':'k'),record=stored(owner,name,secret);
        if(record.length()&&!identityMatches(record,owner,name)){result.error="storage_collision";return result;}
        // Reserve keys before writing values: failed writes may consume a slot,
        // but cannot create unindexed data and escape the per-app NVS quota.
        const String indexKey=keyFor(owner,"$index",secret?'s':'k');
        const String indexRecord=secret?config.getSecretString(indexKey.c_str()):config.getString(indexKey.c_str());
        JsonDocument index;
        if(indexRecord.length()&&(deserializeJson(index,indexRecord)||index["owner"].as<String>()!=owner)) {
            result.error="storage_index";return result;
        }
        index["owner"]=owner;
        JsonArray names=index["names"].is<JsonArray>()?index["names"].as<JsonArray>():index["names"].to<JsonArray>();
        int found=-1;for(size_t i=0;i<names.size();++i)if(names[i].as<String>()==name)found=i;
        const bool deleting=operation==2||operation==5;
        if(!deleting&&found<0) {
            if(names.size()>=(secret?16:64)){result.error="storage_quota";return result;}
            names.add(name);String serializedIndex;serializeJson(index,serializedIndex);
            const bool saved=secret?config.putSecretString(indexKey.c_str(),serializedIndex):config.putString(indexKey.c_str(),serializedIndex);
            if(!saved){result.error="storage_index_write";return result;}
        }
        if(operation==2||operation==5)result.ok=secret?config.removeSecret(key.c_str()):config.remove(key.c_str());
        else {
            JsonDocument doc;doc["owner"]=owner;doc["name"]=name;doc["value"]=value;String serialized;serializeJson(doc,serialized);
            result.ok=secret?config.putSecretString(key.c_str(),serialized):config.putString(key.c_str(),serialized);
        }
        if(deleting&&result.ok&&found>=0) {
            names.remove(found);String serializedIndex;serializeJson(index,serializedIndex);
            if(secret)config.putSecretString(indexKey.c_str(),serializedIndex);else config.putString(indexKey.c_str(),serializedIndex);
        }
        if(!result.ok)result.error="storage_write";return result;
    }
    if(!fileNameValid(name)){result.error="file_path";return result;}
    auto& store=StorageService::instance();const String root="apps/"+owner,path=root+"/"+name;
    auto* fs=store.fsFor(StorageClass::UserData);
    if(!fs){result.error="storage_unavailable";return result;}
    const String absolute=store.pathFor(StorageClass::UserData,path);
    File file=fs->open(absolute,FILE_READ);
    if(file&&file.isDirectory()){file.close();result.error="file_directory";return result;}
    const bool exists=static_cast<bool>(file);const size_t size=exists?file.size():0;
    if(operation==13){result.ok=true;result.value=exists?"true":"false";file.close();return result;}
    if(operation==15){result.ok=exists;result.size=size;result.value=String(size);file.close();return result;}
    if(operation==14){file.close();result.ok=!exists||fs->remove(absolute);return result;}
    const size_t limit=store.usesSd(StorageClass::UserData)?16384:8192;
    if(operation==10) {
        if(!exists||size>limit){file.close();result.error="file_missing_or_limit";return result;}
        result.value.reserve(size);char buffer[256];
        while(file.available()){const int got=file.readBytes(buffer,sizeof(buffer));if(got<=0)break;if(!result.value.concat(buffer,got))break;}
        file.close();result.ok=result.value.length()==size;result.size=size;return result;
    }
    String data=value;
    if(operation==12&&exists){if(size+value.length()>limit){file.close();result.error="file_limit";return result;}data=file.readString()+value;}
    file.close();size_t total=0;unsigned files=0;
    if(data.length()>limit||!usage(*fs,store.pathFor(StorageClass::UserData,root),total,files)||
       total-size+data.length()>(store.usesSd(StorageClass::UserData)?262144:32768)||(!exists&&files>=32)) {
        result.error="file_quota";return result;
    }
    result.ok=store.writeBinaryAtomic(StorageClass::UserData,path,data.c_str(),data.length());
    result.size=data.length();
    if(!result.ok)result.error="file_write";return result;
}
String luaStringForNative(const String& owner,const String& name) {
    bool found=false;const String value=readValue(owner,name,false,found);
    JsonDocument doc;if(!found||deserializeJson(doc,value)||doc["type"].as<int>()!=0)return "";
    return doc["data"].as<String>();
}
void registerLuaStorageBridge(lua_State* L) {
    auto* app=LuaApp::self(L);if(!app)return;
    if(app->allows(AppPermission::UserStorage)) {
        lua_register(L,"hp_kv_get_string",stringGet);lua_register(L,"hp_kv_set_string",stringSet);
        lua_register(L,"hp_kv_get_int",intGet);lua_register(L,"hp_kv_set_int",intSet);
        lua_register(L,"hp_kv_get_bool",boolGet);lua_register(L,"hp_kv_set_bool",boolSet);
        lua_register(L,"hp_kv_get_float",floatGet);lua_register(L,"hp_kv_set_float",floatSet);
        lua_register(L,"hp_kv_exists",kvExists);lua_register(L,"hp_kv_delete",kvDelete);
        lua_register(L,"hp_file_read",fileRead);lua_register(L,"hp_file_write",fileWrite);lua_register(L,"hp_file_append",fileAppend);
        lua_register(L,"hp_file_exists",fileExists);lua_register(L,"hp_file_delete",fileDelete);lua_register(L,"hp_file_size",fileSize);
    }
    if(app->allows(AppPermission::Secrets)){lua_register(L,"hp_secret_set",secretSet);lua_register(L,"hp_secret_exists",secretExists);lua_register(L,"hp_secret_delete",secretDelete);}
}
}
