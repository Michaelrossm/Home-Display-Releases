from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

def stripped(path):
    return '\n'.join(line for line in (ROOT / path).read_text(encoding='utf-8').splitlines()
                     if not line.startswith('#include') and line != '#pragma once')

class CalendarMemoryTests(unittest.TestCase):
    def test_real_query_uses_psram_and_fails_without_abort(self):
        fixture = test_system_performance.SystemPerformanceTests().string_fixture()
        fixture = fixture.replace('size_t length()const{return size();}',
            'size_t length()const{return size();}\n bool equalsIgnoreCase(const char* value)const{String a=*this,b=value;a.toLowerCase();b.toLowerCase();return a==b;}')
        fixture += r'''
#include <mutex>
struct SecurityWriteGuard {};
#include <ArduinoJson.h>
namespace ArduinoJson {template<>struct Converter<String>{static String fromJson(JsonVariantConst v){return v.as<const char*>()?v.as<const char*>():"";}static void toJson(const String& s,JsonVariant v){v.set(std::string(s));}static bool checkJson(JsonVariantConst v){return v.is<const char*>();}};}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4;
bool psramAvailable=true,internalAvailable=false;unsigned allocationCalls=0,frees=0;
size_t allocationSize=0;int allocationFlags=0;
void* heap_caps_malloc(size_t size,int flags){
 ++allocationCalls;allocationSize=size;allocationFlags=flags;
 if((flags&MALLOC_CAP_SPIRAM)?!psramAvailable:!internalAvailable)return nullptr;
 return malloc(size);
}
void heap_caps_free(void* pointer){if(pointer){++frees;free(pointer);}}
uint32_t millis(){return 100;}
size_t strlcpy(char* out,const char* in,size_t size){size_t n=strlen(in);if(size){memcpy(out,in,std::min(n,size-1));out[std::min(n,size-1)]=0;}return n;}
'''
        fixture += stripped('src/apps/calendar/CalendarDate.h')
        fixture += stripped('src/apps/calendar/CalendarTypes.h')
        fixture += stripped('src/apps/calendar/EventDetails.h')
        fixture += stripped('src/services/ContactProtocol.h')
        fixture += stripped('src/apps/calendar/ContactBirthday.h')
        fixture += stripped('src/core/PsramArray.h')
        fixture += r'''
namespace hp {
static_assert(sizeof(EventRecord)*kCloudCalendarEvents==52224,"Captured allocation size");
struct PhonebookContact{String id,name,email;ContactProtocol::Birthday birthday;};
struct LocalPhonebook{std::vector<PhonebookContact> contacts;};
struct PhonebookService{LocalPhonebook data;bool healthy=true;static PhonebookService& instance(){static PhonebookService s;return s;}bool loadLocal(LocalPhonebook& out){out=data;return healthy;}};
namespace calendar {struct Region{};template<class F>void holidays(int,const Region&,F){}JsonDocument profileOptions;bool loadPersonOptions(JsonDocument& d){d=profileOptions;return true;}}
struct StoredCalendarEvent {EventRecord event{};calendar::Rules rules{};};
struct CalendarEventStore {
 template<class F>static bool visit(F f,String&){StoredCalendarEvent item;item.event.localId=1;
 item.event.calendarId=1;item.event.startUtc=110000;item.event.endUtc=120000;
 return f(item);}
};
struct NetworkWorker{static NetworkWorker& instance(){static NetworkWorker w;return w;}void maintain(){}};
struct CalendarSyncService {
 static CalendarSyncService& instance(){static CalendarSyncService s;return s;}
 size_t copyEvents(unsigned slot,EventRecord* out,size_t capacity){
  if(slot||!capacity)return 0;out[0]={};out[0].localId=2;out[0].calendarId=1;
  out[0].startUtc=130000;out[0].endUtc=140000;return 1;
 }
};
struct CalendarService {
 static std::recursive_mutex& profileMutex(){static std::recursive_mutex value;return value;}
 struct Occurrence {EventRecord event{};int64_t originalStartUtc=0;};
 struct Query {int64_t fromUtc=100000,toUtc=300000,afterStartUtc=0;uint64_t afterId=0;
 uint16_t offset=0,limit=32;uint32_t calendarId=0,personId=0;uint32_t selectedCalendars[4]{};uint8_t selectedCalendarCount=0,preset=0;
 const std::atomic<bool>* cancelled=nullptr;};
 std::recursive_mutex mutex_;bool storageHealthy_=true;size_t calendarCount_=1,personCount_=0;
 CalendarRecord calendars_[1]{};PersonRecord persons_[1]{};
 CalendarService(){calendars_[0].id=1;}
 void refreshExternalCalendars(){}calendar::Region region(){return {};}
 uint16_t participantMaskForPersonId(uint32_t id)const{return id==42?1:0;}
 bool query(const Query&,std::vector<Occurrence>&,bool&,String&);
};
bool ordered(const CalendarService::Occurrence& a,const CalendarService::Occurrence& b){
 return a.event.startUtc!=b.event.startUtc?a.event.startUtc<b.event.startUtc:a.event.localId<b.event.localId;
}
'''
        source = (ROOT / 'src/apps/calendar/CalendarOperations.cpp').read_text(encoding='utf-8')
        query = 'bool CalendarService::query' + source.split('bool CalendarService::query',1)[1]
        case = r'''
int main(){using namespace hp;CalendarService service;CalendarService::Query query;
 std::vector<CalendarService::Occurrence> out;bool more=false;String error;
 assert(service.query(query,out,more,error));assert(out.size()==2&&!more);
 assert(allocationSize==52224&&allocationCalls==1&&(allocationFlags&MALLOC_CAP_SPIRAM)&&frees==1);
 query.personId=42;service.calendars_[0].ownerPersonId=42;service.calendars_[0].kind=uint8_t(CalendarKind::External);query.preset=2;
 assert(service.query(query,out,more,error)&&out.size()==2&&out[0].event.ownerPersonId==42);
 query.personId=43;assert(service.query(query,out,more,error)&&out.empty());
 query.preset=0;query.selectedCalendarCount=1;query.selectedCalendars[0]=1;assert(service.query(query,out,more,error)&&out.size()==2);
 query.selectedCalendars[0]=999;assert(service.query(query,out,more,error)&&out.empty());query.selectedCalendarCount=0;
 // Shared features must survive person, personal preset and ordinary calendar filters.
 service.calendars_[0].kind=uint8_t(CalendarKind::BirthdaysVirtual);query.personId=43;query.preset=2;query.calendarId=999;
 query.selectedCalendarCount=1;query.selectedCalendars[0]=999;assert(service.query(query,out,more,error)&&out.size()==2);
 service.calendars_[0].kind=uint8_t(CalendarKind::HolidaysVirtual);assert(service.query(query,out,more,error)&&out.size()==2);
 service.calendars_[0].enabled=0;assert(service.query(query,out,more,error)&&out.empty());service.calendars_[0].enabled=1;
 service.calendars_[0].kind=uint8_t(CalendarKind::Personal);query.calendarId=0;query.selectedCalendarCount=0;
 query.personId=0;query.preset=0;service.calendars_[0].ownerPersonId=0;
 allocationCalls=1;frees=1;
 psramAvailable=false;internalAvailable=false;error="";
 assert(!service.query(query,out,more,error));assert(error=="memory"&&out.empty());
 assert(allocationCalls==3&&frees==1);
 internalAvailable=true;error="";assert(service.query(query,out,more,error));
 assert(out.size()==2&&frees==2&&(allocationFlags&MALLOC_CAP_INTERNAL));
 std::atomic<bool> cancelled{true};query.cancelled=&cancelled;error="";auto before=allocationCalls;
 assert(!service.query(query,out,more,error));assert(out.empty()&&allocationCalls==before);
 query.cancelled=nullptr;query.fromUtc=calendar::localEpoch({2025,2,1});query.toUtc=calendar::localEpoch({2025,3,1});error="";
 service.calendars_[0].kind=uint8_t(CalendarKind::BirthdaysVirtual);
 auto& book=PhonebookService::instance();book.data.contacts.push_back({"stable","Anna","",{2000,2,29}});
 assert(service.query(query,out,more,error));assert(out.size()==1&&out[0].event.flags==(EventAllDay|EventReadOnly));
 assert(calendar::localDate(out[0].event.startUtc).day==28);const auto birthdayId=out[0].event.localId;
 assert(service.query(query,out,more,error)&&out[0].event.localId==birthdayId);
 service.personCount_=1;service.persons_[0].id=42;service.persons_[0].birthdayYear=2000;service.persons_[0].birthdayMonth=2;service.persons_[0].birthdayDay=28;strcpy(service.persons_[0].email,"profile-opaque@calendar.local");strcpy(service.persons_[0].displayName,"Anna");
 book.data.contacts[0].birthday.day=28;calendar::profileOptions["contacts"]["0000002a"]="stable";
 assert(service.query(query,out,more,error)&&out.size()==1);service.personCount_=0;
 book.data.contacts.clear();assert(service.query(query,out,more,error)&&out.empty());
 book.healthy=false;assert(!service.query(query,out,more,error)&&error=="contact_storage_unavailable");
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture + query + case,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_both_calendar_cloud_snapshot_paths_use_checked_allocator(self):
        source = (ROOT / 'src/apps/calendar/CalendarOperations.cpp').read_text(encoding='utf-8')
        self.assertEqual(source.count('allocatePsramArray<EventRecord>(kCloudCalendarEvents)'),2)
        self.assertNotIn('new(std::nothrow) EventRecord[kCloudCalendarEvents]',source)
        self.assertIn('decltype(&heap_caps_free)',source)

if __name__ == '__main__':
    unittest.main()
