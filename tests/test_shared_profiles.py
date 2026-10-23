from pathlib import Path
import unittest
import test_system_performance
from test_native_phonebook import function

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def stripped(path):return '\n'.join(x for x in read(path).splitlines() if not x.startswith('#include') and x!='#pragma once')+'\n'

def program(actual_users=False):
    prelude='bool denyOptionsCopy=false;\n'+read('tests/native/printer_fake.h').split('namespace hp {',1)[0]
    prelude=prelude.replace('size_t length()const{return size();}',r'''size_t length()const{return size();}
    bool equalsIgnoreCase(const char* p)const{String a=*this,b=p;a.toLowerCase();b.toLowerCase();return a==b;}
    bool equalsIgnoreCase(const String& p)const{return equalsIgnoreCase(p.c_str());}
    void concat(const char* p,size_t n){if(!denyOptionsCopy)append(p,n);}''')
    prelude+=r'''
#include <mutex>
#include <functional>
#include <stdexcept>
#include <ctime>
String operator+(const String& value,unsigned n){return String(std::string(value)+std::to_string(n));}
void* heap_caps_calloc(size_t n,size_t size,int){return calloc(n,size);}
uint32_t esp_random(){static uint32_t value=100;return ++value;}
struct File{std::shared_ptr<std::vector<uint8_t>> bytes;size_t position=0;
 explicit operator bool()const{return bool(bytes);}size_t size(){return bytes?bytes->size():0;}
 int available(){return bytes?int(bytes->size()-position):0;}int read(){return available()?(*bytes)[position++]:-1;}
 size_t read(uint8_t* p,size_t n){n=std::min(n,size()-position);if(n)memcpy(p,bytes->data()+position,n);position+=n;return n;}
 size_t readBytes(char* p,size_t n){return read(reinterpret_cast<uint8_t*>(p),n);}void close(){}
};
namespace fs{struct FS{std::map<String,std::shared_ptr<std::vector<uint8_t>>> files;
 File open(const String& name,const char*){auto it=files.find(name);return it==files.end()?File{}:File{it->second};}
 bool exists(const String& p){return files.count(p);}bool remove(const String& p){files.erase(p);return true;}
};}
constexpr const char* FILE_READ="r";
constexpr unsigned HOMEPANEL_CALENDAR_MAX_PERSONS=16,HOMEPANEL_CALENDAR_MAX_CALENDARS=32;
namespace hp {
enum class StorageClass{UserData};enum class StorageArea{UserData};
struct StorageService{fs::FS disk;unsigned writes=0,failAt=0;bool failRemove=false;static StorageService& instance(){static StorageService s;return s;}
 using Validator=std::function<bool(fs::FS&,const String&)>;
 fs::FS* fsFor(StorageClass){return &disk;}String pathFor(StorageClass,const String& p){return p;}
 bool exists(StorageArea,const String& p){return disk.exists(p);}
 bool writeAtomic(StorageArea,const String& p,const void* data,size_t n,const Validator& validate={}){
  if(++writes==failAt)return false;auto value=std::make_shared<std::vector<uint8_t>>(n);if(n)memcpy(value->data(),data,n);disk.files[p+".tmp"]=value;
  if(validate&&!validate(disk,p+".tmp")){disk.remove(p+".tmp");return false;}disk.files[p]=value;disk.remove(p+".tmp");return true;
 }
 bool recoverAtomicWrite(StorageArea,const String& p,const Validator& validate){for(const char* suffix:{"",".bak",".tmp"}){const String name=p+suffix;if(disk.exists(name)&&(!validate||validate(disk,name))){disk.files[p]=disk.files[name];if(name!=p)disk.remove(name);return true;}}return false;}
 bool remove(StorageArea,const String& p){return !failRemove&&disk.remove(p);}
};
struct ConfigService{std::map<String,String> values;bool fail=false;static ConfigService& instance(){static ConfigService s;return s;}String getString(const char* key,const char* fallback){auto it=values.find(key);return it==values.end()?String(fallback):it->second;}bool putString(const char* key,const String& v){if(fail)return false;assert(strlen(key)<=15);values[key]=v;return true;}};
struct SecurityWriteGuard{};
struct SecurityService{bool allowed=true,locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};
struct NetworkWorker{bool worker=true;static NetworkWorker& instance(){static NetworkWorker s;return s;}bool isWorkerContext(){return worker;}};
}
'''
    prelude+=stripped('src/apps/calendar/CalendarTypes.h')
    prelude+=stripped('src/apps/calendar/EventDetails.h')
    prelude+='\n#include "'+(ROOT/'src/apps/calendar/CalendarDate.h').as_posix()+'"\n'
    prelude+=stripped('src/apps/calendar/CalendarMetadataStore.h')
    prelude+=stripped('src/apps/calendar/CalendarPersonOptions.h')
    prelude+=stripped('src/services/PersonName.h')
    prelude+='\n#include "'+(ROOT/'src/services/ContactProtocol.cpp').as_posix()+'"\n'
    book_header=read('src/services/PhonebookService.h')
    prelude+='namespace hp {\n'+book_header[book_header.index('struct PhonebookContact'):book_header.index('// Per-account phonebook:')]+r'''
struct StoredCalendarEvent{struct{uint32_t ownerPersonId=0,calendarId=0;uint16_t participantMask=0;}event;};
struct CalendarEventStore{static std::vector<StoredCalendarEvent> events;static bool visit(const std::function<bool(const StoredCalendarEvent&)>& fn,String&){for(const auto& e:events)if(!fn(e))break;return true;}};
std::vector<StoredCalendarEvent> CalendarEventStore::events;
struct CalendarService{std::recursive_mutex mutex_;bool storageHealthy_=true;PersonRecord persons_[16]{};CalendarRecord calendars_[32]{};size_t personCount_=0,calendarCount_=0;unsigned bumps=1;
 static CalendarService& instance(){static CalendarService s;return s;}static std::recursive_mutex& profileMutex(){static std::recursive_mutex m;return m;}
 bool storageHealthy(){return storageHealthy_;}uint32_t metadataRevision(){return bumps;}void bumpRevision(){++bumps;}
 bool recoverProfiles();bool saveProfile(JsonVariantConst,JsonDocument&,String&);bool removeProfile(uint32_t,bool,String&);
 bool migratePhonebookUsers(LocalPhonebook&,String&);
 bool commitProfileSnapshot(const PersonRecord*,size_t,const CalendarRecord*,size_t,const String&,const String&,String&);
 bool loadPersons(){bool present=false;return calendar::loadMetadata("/calendar/persons.v2.bin",persons_,personCount_,present);}
 bool loadCalendars(){bool present=false;return calendar::loadMetadata("/calendar/calendars.v2.bin",calendars_,calendarCount_,present);}
 void copyMetadata(std::vector<PersonRecord>& p,std::vector<CalendarRecord>& c){p.clear();for(size_t i=0;i<personCount_;++i)if(persons_[i].flags&1)p.push_back(persons_[i]);c.assign(calendars_,calendars_+calendarCount_);}
 int personIndexById(uint32_t id){for(size_t i=0;i<personCount_;++i)if(persons_[i].id==id&&(persons_[i].flags&1))return int(i);return -1;}
 void reboot(){personCount_=calendarCount_=0;storageHealthy_=false;assert(recoverProfiles());assert(loadPersons()&&loadCalendars());storageHealthy_=true;}
};
'''
    prelude+=book_header[book_header.index('class PhonebookService'):book_header.rindex('}')]+ '\n}\n'
    phone=read('src/services/PhonebookService.cpp')
    helper=phone[phone.index('constexpr size_t limit='):phone.index('bool PhonebookService::sharedUsers')]
    helper=helper.replace('}\nPhonebookService&','}\nPhonebookService&')
    # Reuse actual codecs/validation/CRUD; only cloud discovery is out of scope.
    prelude+='namespace hp { namespace {\n'+helper
    prelude+='\n'+function(phone,'bool PhonebookService::decodeLocal(')+function(phone,'bool PhonebookService::validateLocalText(')+function(phone,'bool PhonebookService::encodeLocal(')
    prelude+=r'''
bool PhonebookService::loadLocal(LocalPhonebook& out){auto& storage=StorageService::instance();auto f=storage.disk.open("/accounts/phonebook_local.json","r");out={};if(f){PsramAllocator allocator;JsonDocument doc(&allocator);if(deserializeJson(doc,f)||!decodeLocal(doc.as<JsonVariantConst>(),out))return false;}std::vector<PersonRecord> p;std::vector<CalendarRecord> c;CalendarService::instance().copyMetadata(p,c);out.users.clear();for(const auto& person:p){char id[9];snprintf(id,sizeof(id),"%08lx",(unsigned long)person.id);out.users.push_back({id,person.displayName});}return true;}
bool PhonebookService::saveLocal(const LocalPhonebook& value){String text;if(!encodeLocal(value,text))return false;if(!StorageService::instance().writeAtomic(StorageArea::UserData,"/accounts/phonebook_local.json",text.c_str(),text.length()))return false;++revision_;return true;}
}
'''
    prelude+=stripped('src/apps/calendar/CalendarProfiles.cpp')
    prelude+='namespace hp {\n'+function(phone,'bool PhonebookService::changeLocal(')+'\n}\n'
    if actual_users:
        start=prelude.index('bool PhonebookService::loadLocal(')
        end=prelude.index('bool PhonebookService::saveLocal(',start)
        prelude=prelude[:start]+function(phone,'bool PhonebookService::loadLocal(')+'\n'+prelude[end:]
        prelude+='\nnamespace hp {\n'+function(phone,'bool PhonebookService::sharedUsers(')+'\n}\n'
    return prelude

class SharedProfilesTests(unittest.TestCase):
    def test_limits_stable_slots_existing_contact_and_retry(self):
        self.native(r'''
int main(){using namespace hp;auto& s=CalendarService::instance();auto& p=PhonebookService::instance();auto& storage=StorageService::instance();JsonDocument a,out;String error;
 LocalPhonebook original;PhonebookContact existing;existing.id="existing";existing.name="Existing Contact";existing.email="old@example.org";existing.birthday={1990,6,7};original.contacts.push_back(existing);assert(p.saveLocal(original));
 a["firstName"]="Existing";a["lastName"]="Contact";a["contact_id"]="existing";a["request_id"]="1234567890abcdef";assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));auto first=s.persons_[0].id;assert(s.persons_[0].birthdayDay==7);LocalPhonebook book;assert(p.loadLocal(book)&&book.contacts.size()==1);
 assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(s.personCount_==1&&s.calendarCount_==1);assert(p.loadLocal(book)&&book.contacts.size()==1);
 a.clear();a["firstName"]="Pending";a["request_id"]="fedcba0987654321";storage.failAt=storage.writes+2;assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error));storage.failAt=0;s.reboot();assert(s.personCount_==2);assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(s.personCount_==2);
 auto middle=s.persons_[1].id;a.clear();a["firstName"]="Third";assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));const auto third=s.persons_[2].id;CalendarEventStore::events.push_back({{third,0,4}});
 assert(s.removeProfile(middle,true,error));assert(s.persons_[2].id==third&&s.personIndexById(third)==2);a["firstName"]="Replacement";assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(s.personCount_==3&&s.persons_[1].id!=middle&&s.personIndexById(third)==2);assert(!s.removeProfile(third,true,error)&&error=="profile_has_events");
 while(s.personCount_<16){a.clear();a["firstName"]="Profile";a["create_calendar"]=false;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));}const auto writes=storage.writes;assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error)&&error=="person_limit");assert(writes==storage.writes&&s.persons_[0].id==first);
}
''')

    def test_calendar_links_and_corrupt_intent_are_protected(self):
        self.native(r'''
int main(){using namespace hp;auto& s=CalendarService::instance();JsonDocument a,out;String error;a["firstName"]="Name";assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));auto id=s.persons_[0].id;
 a["id"]=out["id"];a["firstName"]="Renamed";a["color"]=3;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(String(s.calendars_[0].name)=="Renamed"&&s.calendars_[0].colorIndex==3);
 strlcpy(s.calendars_[0].name,"Custom",sizeof(s.calendars_[0].name));s.calendars_[0].colorIndex=7;a["firstName"]="Again";a["color"]=4;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(String(s.calendars_[0].name)=="Custom"&&s.calendars_[0].colorIndex==7);
s.calendars_[1]={};s.calendars_[1].id=11;s.calendars_[1].kind=3;s.calendars_[2]={};s.calendars_[2].id=12;s.calendars_[2].kind=5;strlcpy(s.calendars_[1].name,"Birthdays",40);strlcpy(s.calendars_[2].name,"Holidays",40);s.calendarCount_=3;
 auto views=a["calendars"].to<JsonArray>();views.add(s.calendars_[0].id);views.add(11u);views.add(12u);assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));JsonDocument options;assert(calendar::loadPersonOptions(options));assert(options["views"][out["id"].as<String>()].size()==1);
 ConfigService::instance().putString("cal_user_0",String("{\"person\":")+String(id)+"}");assert(!s.removeProfile(id,true,error)&&error=="profile_has_accounts");ConfigService::instance().values.erase("cal_user_0");
 auto& storage=StorageService::instance();const uint8_t invalid[]={1,2,3};assert(storage.writeAtomic(StorageArea::UserData,"/calendar/profile.commit.bin",invalid,sizeof(invalid)));assert(!s.recoverProfiles()&&!s.storageHealthy_);assert(storage.disk.exists("/calendar/profile.commit.bin"));
}
''')

    def native(self,body):
        test_system_performance.SystemPerformanceTests().native(program()+body,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_actual_atomic_profile_contact_calendar_and_restart(self):
        self.native(r'''
int main(){using namespace hp;auto& s=CalendarService::instance();JsonDocument a,result;String error;
 a["firstName"]="Max";a["lastName"]="Mustermann";a["phone"]="123";a["mobile"]="456";a["contact_email"]="privat@example.org";a["street"]="Hauptstrasse";a["houseNumber"]="12a";a["postalCode"]="93495";a["city"]="Weiding";a["region"]="Bayern";a["country"]="DE";
 assert(s.saveProfile(a.as<JsonVariantConst>(),result,error));assert(s.personCount_==1&&s.calendarCount_==1);auto id=s.persons_[0].id;auto email=String(s.persons_[0].email);assert(email.startsWith("profile-"));LocalPhonebook book;assert(PhonebookService::instance().loadLocal(book));assert(book.contacts.size()==1&&book.contacts[0].phone=="123"&&book.contacts[0].addresses[0].region=="Bayern");
 a["id"]=result["id"];a["contact_email"]="arbeit@example.org";a["display_name"]="Max Arbeit";a["revision"]=s.persons_[0].revision;assert(s.saveProfile(a.as<JsonVariantConst>(),result,error));assert(s.persons_[0].id==id&&s.persons_[0].email==email);s.reboot();assert(s.persons_[0].id==id);assert(PhonebookService::instance().loadLocal(book)&&book.contacts.size()==1&&book.contacts[0].email=="arbeit@example.org");
 a["revision"]=0;assert(!s.saveProfile(a.as<JsonVariantConst>(),result,error)&&error=="profile_revision_conflict");
 a.clear();a["firstName"]="Zweite";a["lastName"]="Person";a["create_calendar"]=false;assert(s.saveProfile(a.as<JsonVariantConst>(),result,error));assert(s.personCount_==2&&s.calendarCount_==1);
 const auto second=s.persons_[1].id;assert(!s.removeProfile(second,false,error));assert(s.removeProfile(second,true,error));assert(s.persons_[1].flags==0);assert(PhonebookService::instance().loadLocal(book)&&book.contacts.size()==2&&book.users.size()==1);
 a["firstName"]="Neue";assert(s.saveProfile(a.as<JsonVariantConst>(),result,error));assert(s.personCount_==2&&s.persons_[1].id!=second);
 SecurityService::instance().allowed=false;assert(!s.saveProfile(a.as<JsonVariantConst>(),result,error)&&error=="settings_locked");SecurityService::instance().allowed=true;
 NetworkWorker::instance().worker=false;assert(!s.saveProfile(a.as<JsonVariantConst>(),result,error));NetworkWorker::instance().worker=true;
 CalendarEventStore::events.push_back({{id,0,1}});assert(!s.removeProfile(id,true,error)&&error=="profile_has_events");
}
''')

    def test_actual_commit_failure_at_each_store_recovers_complete_profile(self):
        self.native(r'''
int main(){using namespace hp;auto& s=CalendarService::instance();auto& storage=StorageService::instance();auto& config=ConfigService::instance();
 for(unsigned fail=1;fail<=6;++fail){storage.disk.files.clear();storage.writes=0;storage.failAt=fail;config.values.clear();config.fail=fail==5;denyOptionsCopy=fail==6;s.personCount_=s.calendarCount_=0;s.storageHealthy_=true;JsonDocument a,out;String error;a["firstName"]="Complete";a["lastName"]="Profile";
  assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error));storage.failAt=0;config.fail=false;denyOptionsCopy=false;
  if(fail==1){assert(s.personCount_==0&&storage.disk.files.empty());continue;}
  assert(!s.storageHealthy_);s.reboot();LocalPhonebook book;assert(PhonebookService::instance().loadLocal(book));assert(s.personCount_==1&&s.calendarCount_==1&&book.contacts.size()==1&&book.users.size()==1);assert(!storage.disk.exists("/calendar/profile.commit.bin"));
 }
}
''')

    def test_actual_linked_contact_edits_and_compatibility_delegate(self):
        self.native(r'''
int main(){using namespace hp;auto& s=CalendarService::instance();auto& p=PhonebookService::instance();JsonDocument a,out;String error;
 a["firstName"]="Anna";a["lastName"]="Alt";a["create_calendar"]=false;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));const auto id=s.persons_[0].id;
 LocalPhonebook book;assert(p.loadLocal(book));auto contact=book.contacts[0];contact.lastName="Neu";contact.email="neu@example.org";contact.birthday={1996,12,24};contact.emails={{"Privat","neu@example.org"},{"Arbeit","work@example.org"}};contact.phones={{"Arbeit","123"},{"Mobile","456"},{"Andere","789"}};
 assert(p.changeLocal(PhonebookService::LocalAction::SaveContact,contact,{},book));assert(s.persons_[0].id==id&&String(s.persons_[0].displayName)=="Anna Neu"&&s.persons_[0].birthdayDay==24);assert(book.contacts[0].emails.size()==2);
 JsonDocument edit;char identity[9];snprintf(identity,sizeof(identity),"%08lx",(unsigned long)id);edit["id"]=identity;edit["phone"]="999";edit["mobile"]="888";edit["contact_email"]="";assert(s.saveProfile(edit.as<JsonVariantConst>(),out,error));assert(p.loadLocal(book));assert(book.contacts[0].phone=="999"&&book.contacts[0].mobile=="888"&&book.contacts[0].phones.size()==3&&book.contacts[0].phones[0].label=="Arbeit"&&book.contacts[0].email=="work@example.org");JsonDocument prefs;assert(calendar::loadPersonOptions(prefs)&&prefs["emails"][identity]=="work@example.org");
 assert(!p.changeLocal(PhonebookService::LocalAction::RemoveContact,contact,{},book));
 PhonebookUser legacy;legacy.name="Legacy";assert(p.changeLocal(PhonebookService::LocalAction::SaveUser,{},legacy,book));assert(s.personCount_==2&&book.contacts.size()==2);
 JsonDocument options;assert(calendar::loadPersonOptions(options));a.clear();a["firstName"]="Linked";a["contact_id"]=book.contacts[0].id;assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error)&&error=="contact_already_linked");
}
''')
