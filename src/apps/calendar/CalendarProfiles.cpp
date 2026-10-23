#include "CalendarService.h"
#include "CalendarMetadataStore.h"
#include "CalendarPersonOptions.h"
#include "../../services/PhonebookService.h"
#include "../../services/PersonName.h"
#include "../../services/NetworkWorker.h"
#include "../../services/SecurityService.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <algorithm>

namespace hp { namespace {
constexpr const char* journalPath="/calendar/profile.commit.bin";
constexpr const char* personsPath="/calendar/persons.v2.bin";
constexpr const char* calendarsPath="/calendar/calendars.v2.bin";
constexpr const char* bookPath="/accounts/phonebook_local.json";
using Persons=calendar::MetadataBlock<PersonRecord,HOMEPANEL_CALENDAR_MAX_PERSONS>;
using Calendars=calendar::MetadataBlock<CalendarRecord,HOMEPANEL_CALENDAR_MAX_CALENDARS>;
struct Header {uint32_t magic=0x48505531,version=1,options=0,book=0,checksum=0;};
constexpr size_t maxJournal=sizeof(Header)+sizeof(Persons)+sizeof(Calendars)+6144+65536;
using Bytes=std::unique_ptr<uint8_t,decltype(&free)>;
String key(uint32_t id){char s[9];snprintf(s,sizeof(s),"%08lx",(unsigned long)id);return s;}
uint32_t idValue(JsonVariantConst v){if(v.is<uint32_t>())return v.as<uint32_t>();const char* p=v.as<const char*>();if(!p||strlen(p)!=8)return 0;uint32_t id=0;for(unsigned i=0;i<8;++i){if(!isxdigit((unsigned char)p[i]))return 0;id=id*16+(p[i]<='9'?p[i]-'0':tolower(p[i])-'a'+10);}return id;}
bool validate(const uint8_t* bytes,size_t size){
 if(size<sizeof(Header)+sizeof(Persons)+sizeof(Calendars)||size>maxJournal)return false;
 Header h;memcpy(&h,bytes,sizeof(h));if(h.magic!=0x48505531||h.version!=1||h.options>6144||h.book>65536||size!=sizeof(Header)+sizeof(Persons)+sizeof(Calendars)+h.options+h.book||
  h.checksum!=detailChecksum(bytes+sizeof(Header),size-sizeof(Header)))return false;
 // Journal structs are copied to aligned storage before access.
 auto p=std::unique_ptr<Persons>(new(std::nothrow) Persons);auto c=std::unique_ptr<Calendars>(new(std::nothrow) Calendars);if(!p||!c)return false;
 memcpy(p.get(),bytes+sizeof(Header),sizeof(Persons));memcpy(c.get(),bytes+sizeof(Header)+sizeof(Persons),sizeof(Calendars));
 if(!p->valid()||!c->valid())return false;
 const auto* text=bytes+sizeof(Header)+sizeof(Persons)+sizeof(Calendars);JsonDocument options;
 return !deserializeJson(options,text,h.options,DeserializationOption::NestingLimit(4))&&calendar::validatePersonOptions(options)&&
  PhonebookService::validateLocalText(text+h.options,h.book);
}
bool loadJournal(fs::FS& fs,const String& path,Bytes& bytes,size_t& size){
 auto file=fs.open(path,"r");if(!file||file.size()>maxJournal)return false;size=file.size();bytes.reset(static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
 return bytes&&file.read(bytes.get(),size)==size&&validate(bytes.get(),size);
}
bool verifyBytes(fs::FS& fs,const String& path,const uint8_t* expected,size_t size){
 auto file=fs.open(path,"r");if(!file||file.size()!=size)return false;uint8_t chunk[512];size_t offset=0;
 while(offset<size){const size_t count=std::min(sizeof(chunk),size-offset);if(file.read(chunk,count)!=count||memcmp(chunk,expected+offset,count))return false;offset+=count;}return true;
}
bool field(JsonVariantConst args,const char* name,String& value,size_t limit){
 if(args[name].isNull())return true;if(!args[name].is<const char*>())return false;const String next=args[name].as<String>();if(next.length()>limit)return false;
 for(unsigned char c:next)if(c<32||c==127)return false;value=next;return true;
}
}

bool CalendarService::recoverProfiles(){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard guard;std::lock_guard<std::recursive_mutex> lock(mutex_);
 auto& storage=StorageService::instance();bool exists=false;for(const char* suffix:{"",".bak",".tmp"})exists|=storage.exists(StorageArea::UserData,String(journalPath)+suffix);if(!exists)return true;
 auto* destination=storage.fsFor(StorageClass::UserData);
 Bytes bytes(nullptr,free);size_t size=0;
 if(!storage.recoverAtomicWrite(StorageArea::UserData,journalPath,[&](fs::FS& fs,const String& path){return loadJournal(fs,path,bytes,size);})){storageHealthy_=false;return false;}
 Header h;memcpy(&h,bytes.get(),sizeof(h));auto* payload=bytes.get()+sizeof(Header);
 // Durable redo intent: either finish all same-format stores or expose no new
 // profiles. Repeated application after power loss is idempotent.
 auto write=[&](const char* path,const uint8_t* data,size_t length){return storage.fsFor(StorageClass::UserData)==destination&&storage.writeAtomic(StorageArea::UserData,path,data,length,[&](fs::FS& fs,const String& target){return verifyBytes(fs,target,data,length);});};
 if(!write(personsPath,payload,sizeof(Persons))||
    !write(calendarsPath,payload+sizeof(Persons),sizeof(Calendars))||
    !write(bookPath,payload+sizeof(Persons)+sizeof(Calendars)+h.options,h.book)) {storageHealthy_=false;return false;}
 String options;options.reserve(h.options);options.concat(reinterpret_cast<const char*>(payload+sizeof(Persons)+sizeof(Calendars)),h.options);
 if(options.length()!=h.options){storageHealthy_=false;return false;}
 if(storage.fsFor(StorageClass::UserData)!=destination||!ConfigService::instance().putString("cal_person_ui",options)||ConfigService::instance().getString("cal_person_ui","")!=options||!storage.remove(StorageArea::UserData,journalPath)){storageHealthy_=false;return false;}
 PhonebookService::instance().notifyLocalChanged();storageHealthy_=loadPersons()&&loadCalendars();return storageHealthy_;
}

bool CalendarService::commitProfileSnapshot(const PersonRecord* people,size_t count,const CalendarRecord* calendars,size_t calendarCount,const String& options,const String& book,String& error){
 Header h;h.options=options.length();h.book=book.length();const size_t size=sizeof(h)+sizeof(Persons)+sizeof(Calendars)+h.options+h.book;
 if(count>HOMEPANEL_CALENDAR_MAX_PERSONS||calendarCount>HOMEPANEL_CALENDAR_MAX_CALENDARS||size>maxJournal){error="profile_limit";return false;}
 Bytes bytes(static_cast<uint8_t*>(heap_caps_calloc(1,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),free);if(!bytes){error="memory";return false;}
 auto p=std::unique_ptr<Persons>(new(std::nothrow) Persons);auto c=std::unique_ptr<Calendars>(new(std::nothrow) Calendars);if(!p||!c){error="memory";return false;}
 p->count=count;memcpy(p->rows,people,count*sizeof(PersonRecord));p->checksum=detailChecksum(p->rows,sizeof(p->rows))^detailChecksum(&p->count,sizeof(p->count));
 c->count=calendarCount;memcpy(c->rows,calendars,calendarCount*sizeof(CalendarRecord));c->checksum=detailChecksum(c->rows,sizeof(c->rows))^detailChecksum(&c->count,sizeof(c->count));
 auto* payload=bytes.get()+sizeof(h);memcpy(payload,p.get(),sizeof(*p));memcpy(payload+sizeof(*p),c.get(),sizeof(*c));
 memcpy(payload+sizeof(*p)+sizeof(*c),options.c_str(),h.options);memcpy(payload+sizeof(*p)+sizeof(*c)+h.options,book.c_str(),h.book);h.checksum=detailChecksum(payload,size-sizeof(h));memcpy(bytes.get(),&h,sizeof(h));
 if(!validate(bytes.get(),size)){error="profile_invalid";return false;}
 auto& storage=StorageService::instance();
 auto* destination=storage.fsFor(StorageClass::UserData);
 if(!storage.writeAtomic(StorageArea::UserData,journalPath,bytes.get(),size,[&](fs::FS& fs,const String& path){Bytes check(nullptr,free);size_t n=0;return loadJournal(fs,path,check,n);})){error="profile_prepare_failed";return false;}
 if(storage.fsFor(StorageClass::UserData)!=destination){storageHealthy_=false;error="profile_commit_pending";return false;}
 if(!recoverProfiles()||!loadPersons()||!loadCalendars()){storageHealthy_=false;error="profile_commit_pending";return false;}
 storageHealthy_=true;bumpRevision();return true;
}

bool CalendarService::saveProfile(JsonVariantConst a,JsonDocument& result,String& error){
 if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!recoverProfiles()||!storageHealthy_){error="profile_storage_unavailable";return false;}
 LocalPhonebook book;if(!PhonebookService::instance().loadLocal(book)){error="contact_storage_unavailable";return false;}
 JsonDocument options;if(!calendar::loadPersonOptions(options)){error="person_preferences_corrupt";return false;}
 std::vector<PersonRecord> next(persons_,persons_+personCount_);std::vector<CalendarRecord> calendars(calendars_,calendars_+calendarCount_);
 uint32_t id=idValue(a["id"]);int index=-1;
 const String request=a["request_id"]|"";
 if(!a["request_id"].isNull()){if(!a["request_id"].is<const char*>()||request.length()!=16){error="profile_request_invalid";return false;}for(char c:request)if(!isxdigit((unsigned char)c)){error="profile_request_invalid";return false;}}
 if(a["id"].isNull()&&request.length())for(size_t i=0;i<next.size();++i)if((next[i].flags&1)&&options["requests"][key(next[i].id)]==request){index=i;id=next[i].id;break;}
 const String legacyEmail=a["email"]|"";
 if(a["id"].isNull()&&legacyEmail.length()){
  for(size_t i=0;i<next.size();++i)if((next[i].flags&1)&&legacyEmail.equalsIgnoreCase(next[i].email)){index=i;id=next[i].id;break;}
  if(index<0&&a["contact_email"].isNull())for(size_t i=0;i<next.size();++i)if((next[i].flags&1)&&legacyEmail.equalsIgnoreCase(options["emails"][key(next[i].id)]|"")){
   if(index>=0){error="person_identity_ambiguous";return false;}index=i;id=next[i].id;
  }
 }
 if(!a["id"].isNull()){for(size_t i=0;i<next.size();++i)if(next[i].id==id&&(next[i].flags&1))index=i;if(index<0){error="person_not_found";return false;}}
 const bool fresh=index<0;
 if(fresh){for(unsigned tries=0;tries<32&&!id;++tries){uint32_t candidate=esp_random();if(candidate&&std::none_of(next.begin(),next.end(),[&](const auto& p){return p.id==candidate;}))id=candidate;}if(!id){error="profile_identity_failed";return false;}
  for(size_t i=0;i<next.size();++i)if(!(next[i].flags&1)){index=i;break;}
  if(index<0){if(next.size()>=16){error="person_limit";return false;}index=next.size();next.push_back({});}next[index]={};next[index].id=id;next[index].flags=1;strlcpy(next[index].email,(String("profile-")+key(id)+"@calendar.local").c_str(),sizeof(PersonRecord::email));
 }
 auto& person=next[index];const String k=key(id);const String oldDisplay=person.displayName;const auto oldColor=person.colorIndex;
 if(!fresh&&!a["revision"].isNull()&&(!a["revision"].is<uint32_t>()||a["revision"].as<uint32_t>()!=person.revision)){error="profile_revision_conflict";return false;}
 String first=options["names"][k]["first"]|person.displayName,last=options["names"][k]["last"]|"",display=person.displayName;
 if(!field(a,"firstName",first,39)||!field(a,"lastName",last,39)||!field(a,"name",display,39)){error="person_name_invalid";return false;}
 if(!a["firstName"].isNull()||!a["lastName"].isNull()){if(a["display_name"].isNull())display=personDisplayName(first,last);}
 if(!field(a,"display_name",display,39)||!display.length()){error="person_name_invalid";return false;}
 if(legacyEmail.length()&&!legacyEmail.equalsIgnoreCase(person.email)&&(!a["contact_email"].isNull()||!calendar::validProfileEmail(legacyEmail.c_str())||(!fresh&&!legacyEmail.equalsIgnoreCase(options["emails"][k]|"")))){error="person_identity_change";return false;}
 const int color=a["color"]|person.colorIndex;if(color<0||color>15){error="color_argument";return false;}
 strlcpy(person.displayName,display.c_str(),sizeof(person.displayName));person.colorIndex=color;++person.revision;
 for(auto& c:calendars)if(c.ownerPersonId==id&&c.kind==uint8_t(CalendarKind::Personal)&&c.provider==uint8_t(CalendarProvider::LocalHomePanel)){
  if(String(c.name)==oldDisplay)strlcpy(c.name,display.c_str(),sizeof(c.name));if(c.colorIndex==oldColor)c.colorIndex=color;
 }
 if(!a["clear_birthday"].isNull()&&!a["clear_birthday"].is<bool>()){error="birthday_invalid";return false;}
 if(a["clear_birthday"]|false){person.birthdayYear=0;person.birthdayMonth=person.birthdayDay=0;}
 else if(!a["birthday_month"].isNull()){const int y=a["birthday_year"]|0,m=a["birthday_month"]|0,d=a["birthday_day"]|0;if(y<0||y>9999||!calendar::validDate(y?y:2000,m,d)){error="birthday_invalid";return false;}person.birthdayYear=y;person.birthdayMonth=m;person.birthdayDay=d;}
 if(!a["contact_email"].isNull()&&!calendar::validProfileEmail(a["contact_email"].as<const char*>())){error="person_email_invalid";return false;}
 if(!a["create_calendar"].isNull()&&!a["create_calendar"].is<bool>()){error="profile_calendar_argument";return false;}
 if(fresh&&(a["create_calendar"]|true)){
  if(calendars.size()>=HOMEPANEL_CALENDAR_MAX_CALENDARS){error="calendar_limit";return false;}
  CalendarRecord c;c.id=id^0x40000000u;if(!c.id||std::any_of(calendars.begin(),calendars.end(),[&](const auto& old){return old.id==c.id;})){error="calendar_identity_conflict";return false;}c.ownerPersonId=id;c.colorIndex=color;strlcpy(c.name,display.c_str(),sizeof(c.name));calendars.push_back(c);
 }
 if(!a["calendars"].isNull()){
  if(!a["calendars"].is<JsonArrayConst>()||a["calendars"].size()>6){error="person_calendar_limit";return false;}
  auto views=options["views"][k].to<JsonArray>();for(auto value:a["calendars"].as<JsonArrayConst>()){const auto calendar=idValue(value);const auto found=std::find_if(calendars.begin(),calendars.end(),[&](const auto& c){return c.id==calendar;});if(found==calendars.end()){error="calendar_not_found";return false;}if(found->kind==3||found->kind==5)continue;for(auto old:views)if(old==key(calendar)){error="calendar_duplicate";return false;}if(views.size()>=4){error="person_calendar_limit";return false;}views.add(key(calendar));}
 }
 String link=options["contacts"][k]|"";if(!a["contact_id"].isNull()){if(!a["contact_id"].is<const char*>()||String(a["contact_id"]|"").length()>64){error="contact_id_invalid";return false;}if(String(a["contact_id"]|"").length())link=a["contact_id"].as<String>();}
 for(auto entry:options["contacts"].as<JsonObject>())if(String(entry.key().c_str())!=k&&entry.value()==link&&link.length()){error="contact_already_linked";return false;}
 auto found=std::find_if(book.contacts.begin(),book.contacts.end(),[&](const auto& c){return c.id==link;});
 if(link.length()&&found==book.contacts.end()){error="contact_not_found";return false;}
 if(found==book.contacts.end()){if(book.contacts.size()>=128){error="contact_limit";return false;}link=String("profile-")+k;if(std::any_of(book.contacts.begin(),book.contacts.end(),[&](const auto& c){return c.id==link;})){error="contact_identity_conflict";return false;}book.contacts.push_back({});found=book.contacts.end()-1;found->id=link;}
 if(!a["contact"].isNull()){PhonebookContact updated;if(!PhonebookService::decodeContact(a["contact"].as<JsonObjectConst>(),updated)||updated.id!=link){error="contact_invalid";return false;}*found=std::move(updated);}
 auto& contact=*found;contact.firstName=first;contact.lastName=last;contact.name=display;contact.userId=k;
 if(fresh&&a["birthday_month"].isNull()&&!(a["clear_birthday"]|false)&&contact.birthday.month){person.birthdayYear=contact.birthday.year;person.birthdayMonth=contact.birthday.month;person.birthdayDay=contact.birthday.day;}
 contact.birthday={person.birthdayYear,person.birthdayMonth,person.birthdayDay};
 if(!field(a,"phone",contact.phone,96)||!field(a,"mobile",contact.mobile,96)||!field(a,"street",contact.street,192)||!field(a,"houseNumber",contact.houseNumber,32)||!field(a,"postalCode",contact.postalCode,32)||!field(a,"city",contact.city,128)||!field(a,"country",contact.country,96)){error="contact_invalid";return false;}
 if(!a["contact_email"].isNull())contact.email=a["contact_email"].as<String>();
 else if(fresh&&legacyEmail.length())contact.email=legacyEmail;
 // Update primary labelled fields without dropping additional numbers/emails.
 auto updateValue=[](std::vector<ContactProtocol::Value>& values,const char* label,const String& value){const bool mobile=!strcmp(label,"Mobil");for(auto it=values.begin();it!=values.end();++it)if((it->label=="Mobil"||it->label=="Mobile")==mobile){if(value.length())it->value=value.c_str();else values.erase(it);return;}if(value.length())values.push_back({label,value.c_str()});};
 if(!a["phone"].isNull())updateValue(contact.phones,"Privat",contact.phone);
 if(!a["mobile"].isNull())updateValue(contact.phones,"Mobil",contact.mobile);
 if(!a["contact_email"].isNull()){if(!contact.emails.empty()){if(contact.email.length())contact.emails.front().value=contact.email.c_str();else contact.emails.erase(contact.emails.begin());}else if(contact.email.length())contact.emails.push_back({"Privat",contact.email.c_str()});}
 // Structured lists are authoritative in the existing contact codec. Mirror the
 // same primary selection so the profile and phonebook never disagree.
 contact.email=contact.emails.empty()?contact.email:String(contact.emails.front().value.c_str());
 if(!contact.phones.empty()){contact.phone="";contact.mobile="";for(const auto& v:contact.phones){if(v.label=="Mobil"||v.label=="Mobile"){if(!contact.mobile.length())contact.mobile=v.value.c_str();}else if(!contact.phone.length())contact.phone=v.value.c_str();}}
 bool address=false;for(const char* f:{"street","houseNumber","postalCode","city","country","region"})address|=!a[f].isNull();
 if(address){if(contact.addresses.empty())contact.addresses.push_back({});auto& address=contact.addresses.front();if(address.label.empty())address.label="Privat";address.street=contact.street.c_str();address.houseNumber=contact.houseNumber.c_str();address.postalCode=contact.postalCode.c_str();address.city=contact.city.c_str();address.country=contact.country.c_str();String region=address.region.c_str();if(!field(a,"region",region,96)){error="contact_invalid";return false;}address.region=region.c_str();}
 options["contacts"][k]=link;options["names"][k]["first"]=first;options["names"][k]["last"]=last;options["emails"][k]=contact.email;
 if(request.length())options["requests"][k]=request;
 book.users.clear();for(const auto& p:next)if(p.flags&1)book.users.push_back({key(p.id),p.displayName});
 String optionText,bookText;serializeJson(options,optionText);if(!calendar::validatePersonOptions(options)||!PhonebookService::encodeLocal(book,bookText)){error="profile_invalid";return false;}
 book={}; // Release contacts before journal validation/readback; never hold two books.
 SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 if(!commitProfileSnapshot(next.data(),next.size(),calendars.data(),calendars.size(),optionText,bookText,error))return false;result["id"]=k;return true;
}

bool CalendarService::removeProfile(uint32_t id,bool confirmed,String& error){
 if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());
 if(!confirmed){error="confirmation_required";return false;}if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 std::lock_guard<std::recursive_mutex> lock(mutex_);const int index=personIndexById(id);if(!storageHealthy_||index<0||!(persons_[index].flags&1)){error="person_not_found";return false;}
 // Never shift participant bits or delete somebody's appointments/accounts.
 std::vector<CalendarRecord> calendars;std::vector<uint32_t> removedCalendars;
 for(size_t i=0;i<calendarCount_;++i){const auto& c=calendars_[i];if(c.ownerPersonId==id){if(c.kind!=uint8_t(CalendarKind::Personal)||c.provider!=uint8_t(CalendarProvider::LocalHomePanel)){error="profile_has_calendars";return false;}removedCalendars.push_back(c.id);}else calendars.push_back(c);}
 for(unsigned slot=0;slot<5;++slot){JsonDocument link;const auto saved=ConfigService::instance().getString((String("cal_user_")+slot).c_str(),"");if(!saved.length())continue;if(saved.length()>1024||deserializeJson(link,saved)||!link["person"].is<uint32_t>()){error="profile_has_accounts";return false;}if(link["person"].as<uint32_t>()==id){error="profile_has_accounts";return false;}}
 bool linked=false;if(!CalendarEventStore::visit([&](const StoredCalendarEvent& e){linked|=e.event.ownerPersonId==id||(e.event.participantMask&(1u<<index))||std::find(removedCalendars.begin(),removedCalendars.end(),e.event.calendarId)!=removedCalendars.end();return true;},error))return false;if(linked){error="profile_has_events";return false;}
 JsonDocument options;if(!calendar::loadPersonOptions(options)){error="person_preferences_corrupt";return false;}
 const String k=key(id);for(auto entry:options["views"].as<JsonObject>())if(String(entry.key().c_str())!=k)for(auto value:entry.value().as<JsonArray>())if(std::find(removedCalendars.begin(),removedCalendars.end(),idValue(value))!=removedCalendars.end()){error="profile_has_links";return false;}
 LocalPhonebook book;if(!PhonebookService::instance().loadLocal(book)){error="contact_storage_unavailable";return false;}
 for(auto& contact:book.contacts)if(contact.userId==k)contact.userId="";
 for(const char* section:{"contacts","views","names","emails","requests"})options[section].as<JsonObject>().remove(k);
 auto next=std::vector<PersonRecord>(persons_,persons_+personCount_);next[index].flags&=~1u;++next[index].revision;
 book.users.clear();for(const auto& p:next)if(p.flags&1)book.users.push_back({key(p.id),p.displayName});String encoded,bookText;serializeJson(options,encoded);if(!PhonebookService::encodeLocal(book,bookText)){error="profile_invalid";return false;}
 SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 return commitProfileSnapshot(next.data(),next.size(),calendars.data(),calendars.size(),encoded,bookText,error);
}

bool CalendarService::migratePhonebookUsers(LocalPhonebook& book,String& error){
 if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard guard;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 std::lock_guard<std::recursive_mutex> lock(mutex_);if(!storageHealthy_){error="profile_storage_unavailable";return false;}
 JsonDocument options;if(!calendar::loadPersonOptions(options)){error="person_preferences_corrupt";return false;}
 auto next=std::vector<PersonRecord>(persons_,persons_+personCount_);auto calendars=std::vector<CalendarRecord>(calendars_,calendars_+calendarCount_);
 auto& contacts=book;bool changed=false;
 for(const auto& user:book.users){
  if(std::any_of(next.begin(),next.end(),[&](const auto& p){return (p.flags&1)&&key(p.id)==user.id;}))continue;
  const String alias="phonebook-"+user.id+"@calendar.local";int index=-1;
  for(size_t i=0;i<next.size();++i)if((next[i].flags&1)&&alias==next[i].email)index=i;
  if(index<0){
   uint32_t id=0;for(unsigned tries=0;tries<32&&!id;++tries){const uint32_t candidate=esp_random();if(candidate&&std::none_of(next.begin(),next.end(),[&](const auto& p){return p.id==candidate;}))id=candidate;}
   if(!id){error="profile_identity_failed";return false;}
   for(size_t i=0;i<next.size();++i)if(!(next[i].flags&1)){index=i;break;}
   if(index<0){if(next.size()>=16){error="person_limit";return false;}index=next.size();next.push_back({});}
   auto& p=next[index];p={};p.id=id;strlcpy(p.email,alias.c_str(),sizeof(p.email));strlcpy(p.displayName,user.name.c_str(),sizeof(p.displayName));
   String first=user.firstName,last=user.lastName;legacyPersonName(user.name,first,last);options["names"][key(id)]["first"]=first;options["names"][key(id)]["last"]=last;
  }
  const String owner=key(next[index].id);for(auto& c:contacts.contacts)if(c.userId==user.id)c.userId=owner;changed=true;
 }
 if(!changed)return true;
 contacts.users.clear();for(const auto& p:next)if(p.flags&1)contacts.users.push_back({key(p.id),p.displayName});
 String encoded,bookText;serializeJson(options,encoded);if(!calendar::validatePersonOptions(options)||!PhonebookService::encodeLocal(contacts,bookText)){error="profile_invalid";return false;}
 contacts={};
 if(!commitProfileSnapshot(next.data(),next.size(),calendars.data(),calendars.size(),encoded,bookText,error))return false;
 return PhonebookService::instance().loadLocal(book);
}
}
