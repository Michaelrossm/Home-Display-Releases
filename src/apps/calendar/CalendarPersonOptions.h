#pragma once
#include "../../services/ConfigService.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cstring>
namespace hp::calendar {
inline bool validProfileEmail(const char* value){if(!value||strlen(value)>63)return false;if(!*value)return true;const char* at=strchr(value,'@');if(!at||at==value||!at[1]||strchr(at+1,'@'))return false;for(const char* c=value;*c;++c)if(static_cast<unsigned char>(*c)<=32||*c==127)return false;return true;}
inline bool validatePersonOptions(JsonDocument& doc){
    if(!doc.is<JsonObject>())return false;
    if(!doc["requests"].isNull()&&!doc["requests"].is<JsonObject>())return false;
    if(doc["requests"].size()>16)return false;for(auto item:doc["requests"].as<JsonObject>()){const char* id=item.key().c_str();const char* token=item.value().as<const char*>();if(strlen(id)!=8||!token||strlen(token)!=16)return false;for(const char* p=id;*p;++p)if(!isxdigit((unsigned char)*p))return false;for(const char* p=token;*p;++p)if(!isxdigit((unsigned char)*p))return false;}
    if(!doc["emails"].isNull()&&!doc["emails"].is<JsonObject>())return false;
    const auto emails=doc["emails"].as<JsonObject>();if(emails.size()>16)return false;
    for(auto email:emails){const char* id=email.key().c_str();if(strlen(id)!=8)return false;for(unsigned i=0;i<8;++i)if(!isxdigit(static_cast<unsigned char>(id[i])))return false;if(!validProfileEmail(email.value().as<const char*>()))return false;}
    const char* format=doc["date_format"]|"iso";
    if(!doc["names"].isNull()&&!doc["names"].is<JsonObject>())return false;
    const auto names=doc["names"].as<JsonObject>();if(names.size()>16)return false;
    for(auto entry:names){const char* id=entry.key().c_str();if(strlen(id)!=8)return false;for(unsigned i=0;i<8;++i)if(!isxdigit(static_cast<unsigned char>(id[i])))return false;
        if(!entry.value().is<JsonObject>())return false;for(const char* part:{"first","last"}){const char* value=entry.value()[part].as<const char*>();if(!value||strlen(value)>39)return false;for(const char* c=value;*c;++c)if(static_cast<unsigned char>(*c)<32||*c==127)return false;}}
    if(strcmp(format,"iso")&&strcmp(format,"de"))return false;
    if(!doc["contacts"].isNull()&&!doc["contacts"].is<JsonObject>())return false;
    const auto links=doc["contacts"].as<JsonObject>();if(links.size()>16)return false;
    for(auto link:links){const char* id=link.key().c_str();if(strlen(id)!=8)return false;for(unsigned i=0;i<8;++i)if(!isxdigit(static_cast<unsigned char>(id[i])))return false;
        const char* contact=link.value().as<const char*>();if(!contact||strlen(contact)>64)return false;}
    if(!doc["views"].isNull()&&!doc["views"].is<JsonObject>())return false;
    auto views=doc["views"].as<JsonObject>();if(views.size()>16)return false;
    for(auto view:views){const char* key=view.key().c_str();if(strlen(key)!=8)return false;for(unsigned i=0;i<8;++i)if(!isxdigit(static_cast<unsigned char>(key[i])))return false;
        if(!view.value().is<JsonArray>()||view.value().size()>4)return false;
        for(auto entry:view.value().as<JsonArray>()){const char* id=entry.as<const char*>();if(!id||strlen(id)!=8)return false;for(unsigned i=0;i<8;++i)if(!isxdigit(static_cast<unsigned char>(id[i])))return false;}
    }
    return true;
}
inline bool loadPersonOptions(JsonDocument& doc){
    const auto saved=ConfigService::instance().getString("cal_person_ui","{}");
    return saved.length()<=6144&&!deserializeJson(doc,saved,DeserializationOption::NestingLimit(4))&&validatePersonOptions(doc);
}
inline bool savePersonOptions(JsonDocument& doc){
    String saved;if(measureJson(doc)>6144)return false;
    serializeJson(doc,saved);return ConfigService::instance().putString("cal_person_ui",saved);
}
}
