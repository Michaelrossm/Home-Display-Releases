#include "PhonebookService.h"
#include "CloudAccountService.h"
#include "StorageService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <memory>
#include <esp_random.h>
#include "../apps/calendar/CalendarService.h"
#include "../apps/calendar/CalendarPersonOptions.h"
#include "../apps/calendar/CalendarApi.h"
#include "PersonName.h"
namespace hp { namespace {
constexpr size_t limit=65536,maxContacts=128;
class PsramAllocator:public ArduinoJson::Allocator{
public:void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
    void deallocate(void* p)override{free(p);}void* reallocate(void* p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
};
String path(const String& id,bool remote=false){return String("/accounts/phonebook_")+id+(remote?"_remote.json":".json");}
bool valid(const PhonebookContact& c){
    if(!validPersonNamePart(c.firstName,256)||!validPersonNamePart(c.lastName,256)||c.company.length()>192||c.jobTitle.length()>128||!ContactProtocol::validBirthday(c.birthday)||c.phones.size()>8||c.emails.size()>8||c.addresses.size()>4)return false;
    auto label=[](const std::string& s){if(s.size()>48)return false;for(unsigned char ch:s)if(ch<32||ch==127)return false;return true;};
    for(const auto& v:c.phones)if(!label(v.label)||v.value.empty()||v.value.size()>96)return false;
    for(const auto& v:c.emails)if(!label(v.label)||v.value.empty()||v.value.size()>254)return false;
    for(const auto& a:c.addresses)if(!label(a.label)||a.street.size()>192||a.houseNumber.size()>32||a.postalCode.size()>32||a.city.size()>128||a.region.size()>96||a.country.size()>96)return false;
    return c.id.length()&&c.id.length()<=128&&c.name.length()<=256&&c.email.length()<=254&&
    c.phone.length()<=96&&c.mobile.length()<=96&&c.street.length()<=192&&c.houseNumber.length()<=32&&
    c.postalCode.length()<=32&&c.city.length()<=128&&c.country.length()<=96&&c.notes.length()<=1024;}
PhonebookContact readContact(JsonObjectConst row,bool remote){PhonebookContact c;
    c.id=row["id"]|"";c.name=row["name"]|"";c.email=row["email"]|"";c.phone=row["phone"]|"";c.mobile=row["mobile"]|"";
    c.firstName=row["firstName"]|"";c.lastName=row["lastName"]|"";legacyPersonName(c.name,c.firstName,c.lastName);
    c.street=row["street"]|"";c.houseNumber=row["houseNumber"]|"";c.postalCode=row["postalCode"]|"";c.city=row["city"]|"";c.country=row["country"]|"";c.notes=row["notes"]|"";c.userId=row["userId"]|"";c.readOnly=remote;
    c.company=row["company"]|"";c.jobTitle=row["jobTitle"]|"";
    if(!row["birthday"].isNull()&&(!row["birthday"].is<const char*>()||!ContactProtocol::parseBirthday(row["birthday"]|"",c.birthday))){c.id="";return c;}
    auto values=[&](const char* key,std::vector<ContactProtocol::Value>& out){
        if(row[key].isNull())return true;if(!row[key].is<JsonArrayConst>()||row[key].size()>8)return false;
        for(JsonVariantConst value:row[key].as<JsonArrayConst>()){if(!value.is<JsonObjectConst>())return false;auto v=value.as<JsonObjectConst>();if(!v["label"].is<const char*>()||!v["value"].is<const char*>())return false;out.push_back({v["label"]|"",v["value"]|""});}return true;};
    if(!values("phones",c.phones)||!values("emails",c.emails)||(!row["addresses"].isNull()&&(!row["addresses"].is<JsonArrayConst>()||row["addresses"].size()>4))){c.id="";return c;}
    // Structured values are authoritative. Keep legacy preview fields in sync,
    // including browser edits/autofill and removal of the final entry.
    if(!row["emails"].isNull())c.email=c.emails.empty()?String():String(c.emails.front().value.c_str());
    if(!row["phones"].isNull()){c.phone="";c.mobile="";for(const auto& value:c.phones){if(value.label=="Mobil"||value.label=="Mobile"){if(!c.mobile.length())c.mobile=value.value.c_str();}else if(!c.phone.length())c.phone=value.value.c_str();}}
    for(JsonVariantConst value:row["addresses"].as<JsonArrayConst>()){if(!value.is<JsonObjectConst>()){c.id="";return c;}auto a=value.as<JsonObjectConst>();
        for(const char* key:{"label","street","houseNumber","postalCode","city","region","country"})if(!a[key].isNull()&&!a[key].is<const char*>()){c.id="";return c;}
        c.addresses.push_back({a["label"]|"",a["street"]|"",a["houseNumber"]|"",a["postalCode"]|"",a["city"]|"",a["region"]|"",a["country"]|""});}
    if(!row["addresses"].isNull()){c.street="";c.houseNumber="";c.postalCode="";c.city="";c.country="";if(!c.addresses.empty()){const auto& a=c.addresses.front();c.street=a.street.c_str();c.houseNumber=a.houseNumber.c_str();c.postalCode=a.postalCode.c_str();c.city=a.city.c_str();c.country=a.country.c_str();}}
    // Old JSON records remain readable; migrate in memory without rewriting on read.
    if(row["phones"].isNull()){if(c.phone.length())c.phones.push_back({"Privat",c.phone.c_str()});if(c.mobile.length())c.phones.push_back({"Mobil",c.mobile.c_str()});}
    if(row["emails"].isNull()&&c.email.length())c.emails.push_back({"Privat",c.email.c_str()});
    if(row["addresses"].isNull()&&(c.street.length()||c.city.length()||c.postalCode.length()||c.country.length()))c.addresses.push_back({"Privat",c.street.c_str(),c.houseNumber.c_str(),c.postalCode.c_str(),c.city.c_str(),"",c.country.c_str()});
    return c;
}
void writeContact(JsonObject row,const PhonebookContact& c){
    row["id"]=c.id;row["name"]=c.name;row["email"]=c.email;row["phone"]=c.phone;row["mobile"]=c.mobile;
    row["firstName"]=c.firstName;row["lastName"]=c.lastName;
    row["street"]=c.street;row["houseNumber"]=c.houseNumber;row["postalCode"]=c.postalCode;
    row["city"]=c.city;row["country"]=c.country;row["notes"]=c.notes;
    if(c.userId.length())row["userId"]=c.userId;
    row["company"]=c.company;row["jobTitle"]=c.jobTitle;row["birthday"]=ContactProtocol::birthdayText(c.birthday);
    auto values=[&](const char* key,const std::vector<ContactProtocol::Value>& items){auto array=row[key].to<JsonArray>();for(const auto& v:items){auto r=array.add<JsonObject>();r["label"]=v.label.c_str();r["value"]=v.value.c_str();}};
    values("phones",c.phones);values("emails",c.emails);
    if(c.phones.empty()){auto a=row["phones"].as<JsonArray>();for(const auto& item:{ContactProtocol::Value{"Privat",c.phone.c_str()},ContactProtocol::Value{"Mobil",c.mobile.c_str()}})if(!item.value.empty()){auto r=a.add<JsonObject>();r["label"]=item.label;r["value"]=item.value;}}
    if(c.emails.empty()&&c.email.length()){auto r=row["emails"].as<JsonArray>().add<JsonObject>();r["label"]="Privat";r["value"]=c.email;}
    auto addresses=row["addresses"].to<JsonArray>();for(const auto& a:c.addresses){auto r=addresses.add<JsonObject>();r["label"]=a.label.c_str();r["street"]=a.street.c_str();r["houseNumber"]=a.houseNumber.c_str();r["postalCode"]=a.postalCode.c_str();r["city"]=a.city.c_str();r["region"]=a.region.c_str();r["country"]=a.country.c_str();}
    if(c.addresses.empty()&&(c.street.length()||c.houseNumber.length()||c.city.length()||c.postalCode.length()||c.country.length())){auto r=addresses.add<JsonObject>();r["label"]="Privat";r["street"]=c.street;r["houseNumber"]=c.houseNumber;r["postalCode"]=c.postalCode;r["city"]=c.city;r["country"]=c.country;}
}
bool validLocal(const LocalPhonebook& data){
    if(data.contacts.size()>maxContacts||data.users.size()>16)return false;
    for(size_t i=0;i<data.users.size();++i){const auto& user=data.users[i];
        if(!user.id.length()||user.id.length()>32||!user.name.length()||user.name.length()>64)return false;
        for(const char* c=user.name.c_str();*c;++c)if(static_cast<unsigned char>(*c)<32||*c==127)return false;
        for(size_t j=0;j<i;++j)if(data.users[j].id==user.id)return false;
    }
    for(size_t i=0;i<data.contacts.size();++i){const auto& c=data.contacts[i];
        if(!valid(c)||!c.name.length()||c.readOnly||c.userId.length()>32)return false;
        for(size_t j=0;j<i;++j)if(data.contacts[j].id==c.id)return false;
        bool owner=!c.userId.length();for(const auto& user:data.users)owner|=user.id==c.userId;
        if(!owner)return false;
    }
    return true;
}
}
PhonebookService& PhonebookService::instance(){static PhonebookService service;return service;}
uint32_t PhonebookService::revision()const{return revision_.load()+CalendarService::instance().metadataRevision();}
bool PhonebookService::decodeContact(JsonObjectConst row,PhonebookContact& out){
    if(row.isNull())return false;
    if(row["readOnly"]|false)return false;
    for(const char* key:{"id","name","firstName","lastName","email","phone","mobile","street","houseNumber","postalCode","city","country","notes","userId","company","jobTitle","birthday"})
        if(!row[key].isNull()&&!row[key].is<const char*>())return false;
    const String id=row["id"]|"";JsonDocument input;input.set(row);if(!id.length())input["id"]="new";
    auto next=readContact(input.as<JsonObjectConst>(),false);
    if(!row["firstName"].isNull()||!row["lastName"].isNull()){next.firstName=row["firstName"]|"";next.lastName=row["lastName"]|"";next.name=personDisplayName(next.firstName,next.lastName);}
    if(!valid(next)||!next.name.length())return false;
    next.id=id;out=std::move(next);return true;
}
void PhonebookService::encodeContact(JsonObject row,const PhonebookContact& contact){writeContact(row,contact);row["readOnly"]=contact.readOnly;}
bool PhonebookService::sharedUsers(LocalPhonebook& data){
    JsonDocument names;if(!calendar::loadPersonOptions(names))return false;
    auto& calendar=CalendarService::instance();if(!calendar.storageHealthy())return false;std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;calendar.copyMetadata(people,calendars);
    auto key=[](uint32_t id){char text[9];snprintf(text,sizeof(text),"%08lx",(unsigned long)id);return String(text);};
    auto appendUser=[&](const PersonRecord& p){PhonebookUser user{key(p.id),p.displayName};user.firstName=names["names"][user.id]["first"]|"";user.lastName=names["names"][user.id]["last"]|"";if(!user.firstName.length()&&!user.lastName.length()){user.firstName=user.name;user.lastName="";}data.users.push_back(std::move(user));};
    struct Migration{String oldId,email;PhonebookUser user;};std::vector<Migration> pending;
    for(const auto& user:data.users){bool found=false;for(const auto& p:people)if(key(p.id)==user.id){found=true;break;}if(found)continue;
        // Legacy random IDs are encoded completely, not reduced to a hash/name match.
        if(user.id.length()>16||user.name.length()>39)return false;
        for(char ch:user.id)if(!isxdigit(static_cast<unsigned char>(ch)))return false;
        const String email="phonebook-"+user.id+"@calendar.local";found=false;
        for(const auto& p:people)if(email==p.email){for(auto& c:data.contacts)if(c.userId==user.id)c.userId=key(p.id);found=true;break;}
        if(!found)pending.push_back({user.id,email,user});
    }
    if(people.size()+pending.size()>16)return false;
    if(!pending.empty()){
        SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()){
            // A read-only calendar query must not fail just because the one-time
            // legacy migration needs an unlocked PIN. Preserve legacy owners until then.
            data.users.clear();for(const auto& p:people)appendUser(p);
            for(const auto& item:pending)data.users.push_back(item.user);return validLocal(data);
        }
        String error;if(!calendar.migratePhonebookUsers(data,error))return false;
        calendar.copyMetadata(people,calendars);
        ++revision_;
    }
    data.users.clear();for(const auto& p:people)appendUser(p);return validLocal(data);
}
bool PhonebookService::load(const String& id,std::vector<PhonebookContact>& output){
    std::vector<PhonebookContact> remote;if(!loadFile(id,false,output))return false;
    if(!loadFile(id,true,remote)){output.clear();return false;}
    for(auto& contact:remote)output.push_back(std::move(contact));return true;
}
bool PhonebookService::loadFile(const String& id,bool remote,std::vector<PhonebookContact>& output){
    output.clear();auto& accounts=CloudAccountService::instance();const auto account=accounts.accountInfo(id);
    if(account.id!=id||!account.enabled||!account.phonebookEnabled||!account.email.length())return false;
    std::unique_ptr<char,decltype(&free)> buffer(static_cast<char*>(heap_caps_malloc(limit+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),free);
    if(!buffer)return false;PsramAllocator allocator;JsonDocument doc(&allocator);
    auto validator=[&](fs::FS& fs,const String& filePath){auto file=fs.open(filePath,"r");if(!file||!file.size()||file.size()>limit)return false;
        const size_t size=file.size();if(file.read(reinterpret_cast<uint8_t*>(buffer.get()),size)!=size)return false;
        if(deserializeJson(doc,buffer.get(),size)||(doc["version"]|0)!=1||String(doc["accountId"]|"")!=id||String(doc["email"]|"")!=account.email||!doc["contacts"].is<JsonArray>())return false;
        auto rows=doc["contacts"].as<JsonArrayConst>();if(rows.size()>maxContacts)return false;std::vector<String> ids;
        for(JsonObjectConst row:rows){auto c=readContact(row,remote);if(!valid(c))return false;for(const auto& prior:ids)if(prior==c.id)return false;ids.push_back(c.id);}return true;};
    auto& storage=StorageService::instance();bool exists=false;for(const char* suffix:{"",".bak",".tmp"})exists|=storage.exists(StorageArea::UserData,path(id,remote)+suffix);
    if(!exists)return true;
    if(!storage.recoverAtomicWrite(StorageArea::UserData,path(id,remote),validator))return false;
    // Recover validates the chosen primary/backup document before exposing it.
    const auto rows=doc["contacts"].as<JsonArrayConst>();if(rows.isNull()||rows.size()>maxContacts)return false;
    std::vector<PhonebookContact> next;next.reserve(rows.size());
    for(JsonObjectConst row:rows){auto c=readContact(row,remote);
        if(!valid(c))return false;for(const auto& prior:next)if(prior.id==c.id)return false;next.push_back(std::move(c));
    }
    const auto current=accounts.accountInfo(id);if(current.email!=account.email||!current.enabled||!current.phonebookEnabled)return false;
    output=std::move(next);return true;
}
bool PhonebookService::save(const String& id,const std::vector<PhonebookContact>& contacts){
    return saveFile(id,false,contacts);
}
bool PhonebookService::saveFile(const String& id,bool remote,const std::vector<PhonebookContact>& contacts,bool truncated){
    SecurityWriteGuard security;auto& accounts=CloudAccountService::instance();const auto account=accounts.accountInfo(id);
    if((!remote&&(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()))||account.id!=id||!account.enabled||!account.phonebookEnabled||!account.email.length()||contacts.size()>maxContacts)return false;
    PsramAllocator allocator;JsonDocument doc(&allocator);doc["version"]=1;doc["accountId"]=id;doc["email"]=account.email;auto rows=doc["contacts"].to<JsonArray>();
    doc["truncated"]=truncated;
    for(size_t i=0;i<contacts.size();++i){const auto& c=contacts[i];if(!valid(c)||(!remote&&c.readOnly))return false;for(size_t j=0;j<i;++j)if(contacts[j].id==c.id)return false;
        writeContact(rows.add<JsonObject>(),c);
    }
    if(doc.overflowed()||measureJson(doc)>limit)return false;String body;serializeJson(doc,body);
    const auto current=accounts.accountInfo(id);if(current.email!=account.email||!current.enabled||!current.phonebookEnabled)return false;
    return StorageService::instance().writeAtomic(StorageArea::UserData,path(id,remote),body.c_str(),body.length());
}
bool PhonebookService::loadLocal(LocalPhonebook& output){
    if(!NetworkWorker::instance().isWorkerContext()||SecurityService::instance().tabletLocked())return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    constexpr const char* filePath="/accounts/phonebook_local.json";
    auto& storage=StorageService::instance();bool exists=false;
    for(const char* suffix:{"",".bak",".tmp"})exists|=storage.exists(StorageArea::UserData,String(filePath)+suffix);
    if(!exists){output={};return sharedUsers(output);}
    PsramAllocator allocator;JsonDocument doc(&allocator);LocalPhonebook next;
    auto validator=[&](fs::FS& fs,const String& path){
        auto file=fs.open(path,"r");if(!file||!file.size()||file.size()>limit)return false;
        doc.clear();next={};
        return !deserializeJson(doc,file,DeserializationOption::NestingLimit(5))&&!doc.overflowed()&&decodeLocal(doc.as<JsonVariantConst>(),next);
    };
    if(!storage.recoverAtomicWrite(StorageArea::UserData,filePath,validator)||SecurityService::instance().tabletLocked())return false;
    if(!sharedUsers(next))return false;output=std::move(next);return true;
}
bool PhonebookService::decodeLocal(JsonVariantConst doc,LocalPhonebook& next){
    if((doc["version"]|0)!=1||String(doc["kind"]|"")!="local"||!doc["contacts"].is<JsonArrayConst>()||!doc["users"].is<JsonArrayConst>()||doc["contacts"].size()>maxContacts||doc["users"].size()>16)return false;
    next={};for(JsonObjectConst row:doc["users"].as<JsonArrayConst>())next.users.push_back({row["id"]|"",row["name"]|""});
    for(JsonObjectConst row:doc["contacts"].as<JsonArrayConst>())next.contacts.push_back(readContact(row,false));return validLocal(next);
}
bool PhonebookService::validateLocalText(const void* bytes,size_t size){
    if(!bytes||size>limit)return false;PsramAllocator allocator;JsonDocument doc(&allocator);
    if(deserializeJson(doc,bytes,size,DeserializationOption::NestingLimit(5))||doc.overflowed()||(doc["version"]|0)!=1||String(doc["kind"]|"")!="local"||!doc["contacts"].is<JsonArray>()||doc["contacts"].size()>maxContacts||!doc["users"].is<JsonArray>())return false;
    LocalPhonebook users;for(JsonObjectConst row:doc["users"].as<JsonArrayConst>())users.users.push_back({row["id"]|"",row["name"]|""});if(!validLocal(users))return false;
    size_t index=0;for(JsonObjectConst row:doc["contacts"].as<JsonArrayConst>()){auto c=readContact(row,false);if(!valid(c)||!c.name.length()||c.userId.length()>32)return false;
        bool owner=!c.userId.length();for(const auto& user:users.users)owner|=user.id==c.userId;if(!owner)return false;
        for(size_t before=0;before<index;++before)if(doc["contacts"][before]["id"]==c.id)return false;++index;
    }return true;
}
bool PhonebookService::encodeLocal(const LocalPhonebook& data,String& body){
    if(!validLocal(data))return false;
    PsramAllocator allocator;JsonDocument doc(&allocator);doc["version"]=1;doc["kind"]="local";
    auto users=doc["users"].to<JsonArray>();for(const auto& user:data.users){auto row=users.add<JsonObject>();row["id"]=user.id;row["name"]=user.name;}
    auto contacts=doc["contacts"].to<JsonArray>();for(const auto& c:data.contacts)writeContact(contacts.add<JsonObject>(),c);
    if(doc.overflowed()||measureJson(doc)>limit)return false;
    body="";serializeJson(doc,body);return true;
}
bool PhonebookService::saveLocal(const LocalPhonebook& data){
    if(!NetworkWorker::instance().isWorkerContext())return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed())return false;
    String body;if(!encodeLocal(data,body))return false;
    if(!StorageService::instance().writeAtomic(StorageArea::UserData,"/accounts/phonebook_local.json",body.c_str(),body.length()))return false;
    ++revision_;return true;
}
bool PhonebookService::loadDirectory(LocalPhonebook& output){
    if(!NetworkWorker::instance().isWorkerContext())return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    if(!loadLocal(output))return false;
    std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;CalendarService::instance().copyMetadata(people,calendars);
    for(unsigned slot=0;slot<5;++slot){const String accountId="account-"+String(slot);const auto info=CloudAccountService::instance().accountInfo(accountId);
        if(!info.enabled||!info.phonebookEnabled||!info.email.length())continue;
        std::vector<PhonebookContact> contacts;if(!load(accountId,contacts))return false;
        String owner;for(const auto& calendar:calendars)if(calendar.id==0x80000000u+slot&&calendar.ownerPersonId){char id[9];snprintf(id,sizeof(id),"%08lx",(unsigned long)calendar.ownerPersonId);owner=id;break;}
        for(auto& contact:contacts){contact.id=accountId+"/"+contact.id;contact.userId=owner;contact.readOnly=true;output.contacts.push_back(std::move(contact));}
    }
    return true;
}
bool PhonebookService::changeLocal(LocalAction action,const PhonebookContact& contact,const PhonebookUser& user,LocalPhonebook& result){
    if(!NetworkWorker::instance().isWorkerContext())return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    SecurityWriteGuard guard;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    LocalPhonebook data;if(!loadLocal(data))return false;
    auto identity=[&](){char id[17];String candidate;bool used;
        for(unsigned attempt=0;attempt<32;++attempt){snprintf(id,sizeof(id),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());candidate=id;used=false;
            for(const auto& c:data.contacts)used|=c.id==candidate;
            for(const auto& u:data.users)used|=u.id==candidate;
            if(!used)return candidate;
        }return String();};
    if(action==LocalAction::SaveUser){
        auto item=user;legacyPersonName(item.name,item.firstName,item.lastName);
        JsonDocument args,out;args["firstName"]=item.firstName;args["lastName"]=item.lastName;
        if(item.id.length()){
            args["id"]=item.id;std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;CalendarService::instance().copyMetadata(people,calendars);
            const String alias="phonebook-"+item.id+"@calendar.local";for(const auto& person:people)if(alias==person.email){char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(person.id));args["id"]=id;break;}
        }String error;
        if(!CalendarService::instance().saveProfile(args.as<JsonVariantConst>(),out,error))return false;
        return loadLocal(result);
    }else if(action==LocalAction::SaveContact){
        auto item=contact;legacyPersonName(item.name,item.firstName,item.lastName);item.name=personDisplayName(item.firstName,item.lastName);if(!item.name.length())return false;
        JsonDocument options;if(!calendar::loadPersonOptions(options))return false;
        for(auto link:options["contacts"].as<JsonObject>())if(link.value()==item.id&&item.id.length()){
            JsonDocument args,out;args["id"]=String(link.key().c_str());args["firstName"]=item.firstName;args["lastName"]=item.lastName;args["contact_email"]=item.email;
            std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;CalendarService::instance().copyMetadata(people,calendars);
            for(const auto& person:people){char key[9];snprintf(key,sizeof(key),"%08lx",static_cast<unsigned long>(person.id));if(args["id"]==key&&String(person.displayName)!=personDisplayName(options["names"][key]["first"]|person.displayName,options["names"][key]["last"]|""))args["display_name"]=String(person.displayName);}
            args["clear_birthday"]=!item.birthday.month;if(item.birthday.month){args["birthday_year"]=item.birthday.year;args["birthday_month"]=item.birthday.month;args["birthday_day"]=item.birthday.day;}
            encodeContact(args["contact"].to<JsonObject>(),item);String error;
            if(!CalendarService::instance().saveProfile(args.as<JsonVariantConst>(),out,error))return false;return loadLocal(result);
        }
        if(!item.id.length()){if(data.contacts.size()>=maxContacts)return false;item.id=identity();if(!item.id.length())return false;data.contacts.push_back(item);}
        else{bool found=false;for(auto& old:data.contacts)if(old.id==item.id){old=item;found=true;}if(!found)return false;}
    }else{
        JsonDocument options;if(!calendar::loadPersonOptions(options))return false;
        for(auto link:options["contacts"].as<JsonObject>())if(link.value()==contact.id)return false;
        bool found=false;for(auto i=data.contacts.begin();i!=data.contacts.end();++i)if(i->id==contact.id){data.contacts.erase(i);found=true;break;}
        if(!found)return false;
    }
    if(!saveLocal(data))return false;result=std::move(data);return true;
}
bool PhonebookService::importVcard(const String& text,const String& userId,LocalPhonebook& result){
    if(!NetworkWorker::instance().isWorkerContext())return false;ContactProtocol::Card card;
    if(!ContactProtocol::parseVcard(text.c_str(),card)||card.name.empty())return false;
    PhonebookContact c;c.name=card.name.c_str();c.email=card.email.c_str();c.phone=card.phone.c_str();c.mobile=card.mobile.c_str();
    c.firstName=card.firstName.c_str();c.lastName=card.lastName.c_str();
    c.street=card.street.c_str();c.postalCode=card.postalCode.c_str();c.city=card.city.c_str();c.country=card.country.c_str();c.notes=card.notes.c_str();
    c.company=card.company.c_str();c.jobTitle=card.jobTitle.c_str();c.birthday=card.birthday;c.userId=userId;
    c.phones=std::move(card.phones);c.emails=std::move(card.emails);c.addresses=std::move(card.addresses);
    // Import creates a contact; a foreign UID never overwrites another local identity.
    return changeLocal(LocalAction::SaveContact,c,{},result);
}
bool PhonebookService::exportVcard(const PhonebookContact& c,String& output)const{
    output="";if(!valid(c))return false;ContactProtocol::Card card;
    card.uid=c.id.c_str();card.name=c.name.c_str();card.notes=c.notes.c_str();card.company=c.company.c_str();card.jobTitle=c.jobTitle.c_str();
    card.firstName=c.firstName.c_str();card.lastName=c.lastName.c_str();
    card.birthday=c.birthday;card.phones=c.phones;card.emails=c.emails;card.addresses=c.addresses;
    if(card.phones.empty()){if(c.phone.length())card.phones.push_back({"Privat",c.phone.c_str()});if(c.mobile.length())card.phones.push_back({"Mobil",c.mobile.c_str()});}
    if(card.emails.empty()&&c.email.length())card.emails.push_back({"Privat",c.email.c_str()});
    if(card.addresses.empty()&&(c.street.length()||c.houseNumber.length()||c.city.length()||c.postalCode.length()||c.country.length()))card.addresses.push_back({"Privat",c.street.c_str(),c.houseNumber.c_str(),c.postalCode.c_str(),c.city.c_str(),"",c.country.c_str()});
    std::string text;if(!ContactProtocol::serializeVcard(card,text))return false;output=text.c_str();return true;
}
namespace {
bool exchangeFilename(const String& filename){
    String extension=filename.substring(filename.length()>=4?filename.length()-4:0);extension.toLowerCase();
    if(filename.length()<5||filename.length()>80||extension!=".vcf")return false;
    for(char c:filename)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))&&c!='-'&&c!='_'&&c!='.')return false;
    return filename.indexOf("..")<0;
}
}
bool PhonebookService::importVcardFile(const String& filename,const String& userId,LocalPhonebook& result){
    if(!NetworkWorker::instance().isWorkerContext()||!exchangeFilename(filename))return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    auto& storage=StorageService::instance();auto* fs=storage.fsFor(StorageArea::UserData);if(!fs)return false;
    auto file=fs->open(storage.pathFor(StorageArea::UserData,"/contacts/"+filename),"r");
    if(!file||!file.size()||file.size()>16384)return false;
    std::unique_ptr<char,decltype(&free)> buffer(static_cast<char*>(heap_caps_malloc(file.size()+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),free);
    if(!buffer)return false;const auto size=file.size();if(file.read(reinterpret_cast<uint8_t*>(buffer.get()),size)!=size)return false;buffer.get()[size]=0;
    if(memchr(buffer.get(),0,size))return false;return importVcard(buffer.get(),userId,result);
}
bool PhonebookService::exportVcardFile(const String& filename,const PhonebookContact& contact,LocalPhonebook& result){
    if(!NetworkWorker::instance().isWorkerContext()||!exchangeFilename(filename))return false;
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    const String path="/contacts/"+filename;auto& storage=StorageService::instance();String body;
    if(storage.exists(StorageArea::UserData,path)||!exportVcard(contact,body)||!loadLocal(result))return false;
    return storage.writeAtomic(StorageArea::UserData,path,body.c_str(),body.length());
}
}
