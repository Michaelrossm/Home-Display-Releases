#include "LuaAppDataCleanupService.h"
#include "ConfigService.h"
#include "StorageService.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

namespace hp {
namespace {

bool validAppId(const String& id){
    if(!id.length()||id.length()>31)return false;
    for(size_t i=0;i<id.length();++i){const char c=id[i];if(!isalnum((unsigned char)c)&&c!='_'&&c!='-')return false;}
    return true;
}
bool validStorageName(const String& name){
    if(!name.length()||name.length()>48)return false;
    for(size_t i=0;i<name.length();++i){const char c=name[i];if(!isalnum((unsigned char)c)&&c!='_'&&c!='-')return false;}
    return true;
}
String keyFor(const String& owner,const String& name,char prefix){
    const String identity=owner+":"+name;unsigned char digest[32]{};
    if(mbedtls_sha256((const unsigned char*)identity.c_str(),identity.length(),digest,0)!=0)return "";
    char key[16]{};key[0]=prefix;for(unsigned i=0;i<7;++i)snprintf(key+1+i*2,3,"%02x",digest[i]);return String(key);
}
bool identityMatches(const String& record,const String& owner,const String& name){
    if(!record.length())return false;JsonDocument doc;if(deserializeJson(doc,record))return false;
    return doc["owner"].as<String>()==owner&&doc["name"].as<String>()==name;
}
String jsonEsc(const String& in){
    String out;out.reserve(in.length()+8);
    for(size_t i=0;i<in.length();++i){switch(in[i]){case '\\':out+="\\\\";break;case '"':out+="\\\"";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if((unsigned char)in[i]>=0x20)out+=in[i];}}
    return out;
}
bool eraseTree(fs::FS& fs,const String& path){
    File node=fs.open(path,FILE_READ);if(!node)return true;
    if(!node.isDirectory()){node.close();return fs.remove(path);}
    File child=node.openNextFile();while(child){const String cp=child.path();const bool dir=child.isDirectory();child.close();
        if(!(dir?eraseTree(fs,cp):fs.remove(cp))){node.close();return false;}child=node.openNextFile();}
    node.close();return fs.rmdir(path);
}
bool loadIndex(const String& owner,bool secret,JsonDocument& index,String& error){
    auto& c=ConfigService::instance();const String ik=keyFor(owner,"$index",secret?'s':'k');
    const String raw=secret?c.getSecretString(ik.c_str(),""):c.getString(ik.c_str(),"");
    if(!raw.length()){index["owner"]=owner;index["names"].to<JsonArray>();return true;}
    if(deserializeJson(index,raw)||index["owner"].as<String>()!=owner||!index["names"].is<JsonArray>()){
        error=secret?"app_secret_index_invalid":"app_kv_index_invalid";return false;}
    return true;
}
bool validateNamespace(const String& owner,bool secret,String& error){
    JsonDocument index;if(!loadIndex(owner,secret,index,error))return false;auto& c=ConfigService::instance();
    for(JsonVariantConst v:index["names"].as<JsonArrayConst>()){
        if(!v.is<const char*>())continue;const String name=v.as<String>();if(!validStorageName(name)){error="app_data_name_invalid";return false;}
        const String key=keyFor(owner,name,secret?'s':'k');const String record=secret?c.getSecretString(key.c_str(),""):c.getString(key.c_str(),"");
        if(record.length()&&!identityMatches(record,owner,name)){error="app_data_identity_collision";return false;}
    }return true;
}
bool eraseNamespace(const String& owner,bool secret,size_t& removed,String& error){
    JsonDocument index;if(!loadIndex(owner,secret,index,error))return false;auto& c=ConfigService::instance();
    for(JsonVariantConst v:index["names"].as<JsonArrayConst>()){
        if(!v.is<const char*>())continue;const String name=v.as<String>();if(!validStorageName(name))continue;
        const String key=keyFor(owner,name,secret?'s':'k');const String record=secret?c.getSecretString(key.c_str(),""):c.getString(key.c_str(),"");
        if(!record.length())continue;if(!identityMatches(record,owner,name)){error="app_data_identity_collision";return false;}
        const bool ok=secret?c.removeSecret(key.c_str()):c.remove(key.c_str());if(!ok){error=secret?"app_secret_remove_failed":"app_kv_remove_failed";return false;}++removed;
    }
    const String ik=keyFor(owner,"$index",secret?'s':'k');
    const bool ok=secret?c.removeSecret(ik.c_str()):c.remove(ik.c_str());
    if(!ok){error=secret?"app_secret_index_remove_failed":"app_kv_index_remove_failed";return false;}return true;
}
esp_err_t chunk(httpd_req_t* req,const String& s){return httpd_resp_send_chunk(req,s.c_str(),s.length());}
bool streamFiles(httpd_req_t* req,fs::FS& fs,const String& base,const String& path,bool& first,unsigned depth,bool committedOnly=false){
    if(depth>4)return false;File node=fs.open(path,FILE_READ);if(!node)return true;
    if(!node.isDirectory()){
        if(committedOnly&&(path.endsWith(".part")||path.endsWith(".tmp")||path.endsWith(".bak")))return true;
        const size_t size=node.size();String rel=path; if(rel.startsWith(base))rel=rel.substring(base.length());while(rel.startsWith("/"))rel.remove(0,1);
        if(!first)httpd_resp_send_chunk(req,",",1);first=false;
        String head="{\"path\":\""+jsonEsc(rel)+"\",\"size\":"+String((unsigned long)size)+",\"base64\":\"";if(chunk(req,head)!=ESP_OK){node.close();return false;}
        uint8_t in[768];unsigned char enc[1028];
        while(node.available()){
            const size_t got=node.read(in,sizeof(in));if(!got)break;size_t outLen=0;
            if(mbedtls_base64_encode(enc,sizeof(enc),&outLen,in,got)!=0){node.close();return false;}
            if(httpd_resp_send_chunk(req,(const char*)enc,outLen)!=ESP_OK){node.close();return false;}
        }
        node.close();return httpd_resp_send_chunk(req,"\"}",2)==ESP_OK;
    }
    File child=node.openNextFile();while(child){String cp=child.path();bool dir=child.isDirectory();child.close();
        if(dir){if(!streamFiles(req,fs,base,cp,first,depth+1,committedOnly)){node.close();return false;}}
        else{if(!streamFiles(req,fs,base,cp,first,depth+1,committedOnly)){node.close();return false;}}
        child=node.openNextFile();}
    node.close();return true;
}
}

LuaAppDataCleanupService& LuaAppDataCleanupService::instance(){static LuaAppDataCleanupService s;return s;}

bool LuaAppDataCleanupService::preflight(const String& appId,String& error){
    if(!validAppId(appId)){error="invalid_app_id";return false;}
    if(!validateNamespace(appId,false,error)||!validateNamespace(appId,true,error))return false;
    auto& st=StorageService::instance();if(auto* fs=st.fsFor(StorageClass::UserData)){
        for(const char* prefix:{"apps/","streams/"}){
            const String root=st.pathFor(StorageClass::UserData,String(prefix)+appId);
            if(fs->exists(root)){File dir=fs->open(root,FILE_READ);if(!dir||!dir.isDirectory()){if(dir)dir.close();error="app_files_invalid";return false;}dir.close();}}
    }
    return true;
}

LuaAppDataCleanupResult LuaAppDataCleanupService::erase(const String& appId){
    LuaAppDataCleanupResult r;String pre;if(!preflight(appId,pre)){r.error=pre;return r;}
    if(!eraseNamespace(appId,false,r.kvRemoved,r.error))return r;
    if(!eraseNamespace(appId,true,r.secretsRemoved,r.error))return r;
    auto& st=StorageService::instance();if(auto* fs=st.fsFor(StorageClass::UserData)){
        for(const char* prefix:{"apps/","streams/"}){
            const String root=st.pathFor(StorageClass::UserData,String(prefix)+appId);
            if(fs->exists(root)){if(!eraseTree(*fs,root)){r.error="app_files_remove_failed";return r;}r.filesRemoved=true;}}
    }
    r.ok=true;return r;
}

esp_err_t LuaAppDataCleanupService::sendExport(httpd_req_t* req,const String& appId){
    String error;if(!req||!preflight(appId,error))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error.c_str());
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    const String disposition="attachment; filename=\""+appId+"-backup.json\"";httpd_resp_set_hdr(req,"Content-Disposition",disposition.c_str());
    httpd_resp_set_hdr(req,"Cache-Control","no-store");

    if(chunk(req,"{\"schema\":1,\"appId\":\""+jsonEsc(appId)+"\",\"note\":\"Secret-Werte werden aus Sicherheitsgruenden nicht exportiert.\",\"settings\":[")!=ESP_OK)return ESP_FAIL;
    JsonDocument index;loadIndex(appId,false,index,error);auto& c=ConfigService::instance();bool first=true;
    for(JsonVariantConst v:index["names"].as<JsonArrayConst>()){
        if(!v.is<const char*>())continue;const String name=v.as<String>();const String key=keyFor(appId,name,'k');const String rec=c.getString(key.c_str(),"");if(!identityMatches(rec,appId,name))continue;
        JsonDocument d;deserializeJson(d,rec);JsonDocument value;if(deserializeJson(value,d["value"].as<String>()))continue;
        String data;serializeJson(value["data"],data);if(!first)httpd_resp_send_chunk(req,",",1);first=false;
        if(chunk(req,"{\"key\":\""+jsonEsc(name)+"\",\"type\":"+String(value["type"].as<int>())+",\"value\":"+data+"}")!=ESP_OK)return ESP_FAIL;
    }
    if(chunk(req,"],\"secrets\":[")!=ESP_OK)return ESP_FAIL;JsonDocument si;loadIndex(appId,true,si,error);first=true;
    for(JsonVariantConst v:si["names"].as<JsonArrayConst>()){if(!v.is<const char*>())continue;if(!first)httpd_resp_send_chunk(req,",",1);first=false;
        if(chunk(req,"{\"key\":\""+jsonEsc(v.as<String>())+"\",\"configured\":true}")!=ESP_OK)return ESP_FAIL;}
    if(chunk(req,"],\"files\":[")!=ESP_OK)return ESP_FAIL;
    auto& st=StorageService::instance();first=true;if(auto* fs=st.fsFor(StorageClass::UserData)){
        const String root=st.pathFor(StorageClass::UserData,"apps/"+appId);if(fs->exists(root)&&!streamFiles(req,*fs,root,root,first,0))return ESP_FAIL;}
    if(chunk(req,"],\"streams\":[")!=ESP_OK)return ESP_FAIL;
    first=true;if(auto* fs=st.fsFor(StorageClass::UserData)){
        const String root=st.pathFor(StorageClass::UserData,"streams/"+appId);
        if(fs->exists(root)&&!streamFiles(req,*fs,root,root,first,0,true))return ESP_FAIL;}
    if(chunk(req,"]}")!=ESP_OK)return ESP_FAIL;return httpd_resp_send_chunk(req,nullptr,0);
}
}
