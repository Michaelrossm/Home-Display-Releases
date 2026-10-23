from pathlib import Path
import unittest
import test_system_performance
from test_calendar_memory import stripped

ROOT=Path(__file__).resolve().parents[1]

class CalendarUsersTests(unittest.TestCase):
    def test_actual_account_link_persistence_scope_and_security(self):
        json_dir=ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'
        if not json_dir.exists():
            self.skipTest('Pinned ArduinoJson dependency required')
        source=(ROOT/'src/apps/calendar/CalendarOperations.cpp').read_text(encoding='utf-8')
        helper='String accountScope'+source.split('String accountScope',1)[1].split('void CalendarService::copyMetadata',1)[0]
        methods='void CalendarService::refreshExternalCalendars'+source.split('void CalendarService::refreshExternalCalendars',1)[1].split('calendar::Region CalendarService::region()',1)[0]
        fixture=test_system_performance.SystemPerformanceTests().string_fixture().replace('size_t length()const{return size();}', 'size_t length()const{return size();} long toInt()const{return strtol(c_str(),nullptr,10);}')
        fixture+=r'''
#include <ArduinoJson.h>
#include <mutex>
String operator+(const String& s,unsigned n){return String(std::string(s)+std::to_string(n));}
namespace ArduinoJson {template<>struct Converter<String>{static String fromJson(JsonVariantConst v){return v.as<const char*>()?v.as<const char*>():"";}static void toJson(const String& s,JsonVariant v){v.set(std::string(s));}static bool checkJson(JsonVariantConst v){return v.is<const char*>();}};}
size_t strlcpy(char* out,const char* in,size_t size){size_t n=strlen(in);if(size){memcpy(out,in,std::min(n,size-1));out[std::min(n,size-1)]=0;}return n;}
constexpr unsigned HOMEPANEL_CALENDAR_MAX_CALENDARS=32;
'''+stripped('src/apps/calendar/CalendarTypes.h')+r'''
namespace hp {
struct SecurityWriteGuard{};
struct SecurityService{bool allowed=true,locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};
struct NetworkWorker{bool worker=true;static NetworkWorker& instance(){static NetworkWorker w;return w;}bool isWorkerContext(){return worker;}};
struct Info{String email="max@example.com";unsigned provider=1;bool configured=true,working=false;};
struct Account{String id="account-0",email="max@example.com";};
struct CalendarSyncService{Info current;String server;static CalendarSyncService& instance(){static CalendarSyncService s;return s;}Info info(unsigned){return current;}String calDavServer(unsigned){return server;}std::vector<Account> accounts(){return {Account{}};}};
struct ConfigService{bool fail=false;unsigned writes=0;std::map<String,String> values;static ConfigService& instance(){static ConfigService c;return c;}String getString(const char* key,const char* fallback){auto i=values.find(key);return i==values.end()?String(fallback):i->second;}bool putString(const char* key,const String& value){if(fail)return false;assert(strlen(key)<=15);values[key]=value;++writes;return true;}};
struct CalendarService{static std::recursive_mutex& profileMutex(){static std::recursive_mutex m;return m;}std::recursive_mutex mutex_;bool storageHealthy_=true;CalendarRecord calendars_[32]{};size_t calendarCount_=0;unsigned revision=0;int personIndexById(unsigned id){return id==42||id==43?0:-1;}bool saveCalendars(){return true;}void bumpRevision(){++revision;}void refreshExternalCalendars();bool linkAccountPerson(uint8_t,uint32_t,String&);};
namespace {
'''+helper+methods+r'''
}
int main(){using namespace hp;CalendarService service;String error;auto& config=ConfigService::instance();auto& sync=CalendarSyncService::instance();
assert(service.linkAccountPerson(0,42,error));assert(service.calendarCount_==1&&service.calendars_[0].ownerPersonId==42);
CalendarService reboot;reboot.refreshExternalCalendars();assert(reboot.calendars_[0].ownerPersonId==42);
const auto writes=config.writes;assert(!service.linkAccountPerson(0,99,error)&&error=="person_not_found"&&config.writes==writes);
SecurityService::instance().allowed=false;assert(!service.linkAccountPerson(0,43,error));SecurityService::instance().allowed=true;
NetworkWorker::instance().worker=false;assert(!service.linkAccountPerson(0,43,error)&&error=="worker_required");NetworkWorker::instance().worker=true;
config.fail=true;assert(!service.linkAccountPerson(0,43,error)&&service.calendars_[0].ownerPersonId==42);config.fail=false;
sync.current.email="other@example.com";service.refreshExternalCalendars();assert(service.calendars_[0].ownerPersonId==0);
sync.current.email="max@example.com";sync.current.provider=2;service.refreshExternalCalendars();assert(service.calendars_[0].ownerPersonId==0);
sync.current.provider=1;sync.server="https://dav.example.com";service.refreshExternalCalendars();assert(service.calendars_[0].ownerPersonId==0);
assert(service.linkAccountPerson(0,43,error));assert(service.calendars_[0].ownerPersonId==43);
assert(service.linkAccountPerson(0,0,error)&&service.calendars_[0].ownerPersonId==0);
sync.current.working=true;assert(!service.linkAccountPerson(0,42,error)&&error=="account_not_ready");
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture, [json_dir])

    def test_display_web_queue_and_optional_person_identity(self):
        app=(ROOT/'src/apps/calendar/CalendarApp.cpp').read_text(encoding='utf-8')
        api=(ROOT/'src/apps/calendar/CalendarApi.cpp').read_text(encoding='utf-8')
        web=(ROOT/'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        self.assertIn('p.controller.action("account_person",args)',app)
        self.assertNotIn('linkAccountPerson(',app)
        self.assertIn("job('account_person'",web)
        self.assertIn('service.linkAccountPerson(slot,person,error)',api)
        self.assertIn('@calendar.local',api)
        self.assertIn('person_identity_change',(ROOT/'src/apps/calendar/CalendarProfiles.cpp').read_text(encoding='utf-8'))
        self.assertIn('return service.saveProfile(a,result,error)',api)
        self.assertIn('p.shownStatusGeneration!=c.statusGeneration',app)
        controller=(ROOT/'src/apps/calendar/CalendarController.cpp').read_text(encoding='utf-8')
        self.assertIn('++statusGeneration',controller)

if __name__=='__main__':unittest.main()
