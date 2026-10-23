#include <deque>
namespace hp {
enum class EmailProvider{Google,Microsoft};
enum class AccountCapability{Calendar};
enum class StorageArea{UserData};
struct SecurityWriteGuard{};
struct Guard{explicit Guard(void*){}};
struct TimeService{static TimeService& instance(){static TimeService s;return s;}int64_t nowUtc(){return 100000;}};
struct NetworkService{bool connected=true;static NetworkService& instance(){static NetworkService s;return s;}bool online(){return connected;}};
struct EmailAccountService{struct Account{bool configured=true;EmailProvider provider=EmailProvider::Google;String address="owner@test.invalid";} value;
 static EmailAccountService& instance(){static EmailAccountService s;return s;}Account account(unsigned){return value;}};
struct StorageService{unsigned writes=0;bool fail=false;static StorageService& instance(){static StorageService s;return s;}
 bool writeAtomic(StorageArea,const String&,const void*,size_t){++writes;return !fail;}};
struct OAuthProtocol{static String utcText(int64_t value){return String(std::to_string(value));}
 static String encode(const String& value){return value;}
 static int64_t utcDate(const char* value){return !strcmp(value,"begin")?100:!strcmp(value,"end")?200:0;}};
String form(const char* key,const String& value){return String(key)+"="+value;}
bool identity(const String& a,const String& b){return a==b;}
void utf8Copy(char* out,size_t capacity,const char* value){detailPreview(out,capacity,value);}
struct SecureAllocator:ArduinoJson::Allocator{
 void* allocate(size_t size)override{return malloc(size);}
 void deallocate(void* value)override{free(value);}
 void* reallocate(void* value,size_t size)override{return realloc(value,size);}
};
unsigned calendarEvents=0;void publishLuaEvent(const char*,const char*){++calendarEvents;}
class CalendarSyncService{
public:
 struct CloudCalendarCollection{String id,displayName;bool readOnly=true;};
 std::vector<CloudCalendarCollection> selected{{"primary","Standard",true}};
 std::vector<CloudCalendarCollection> collections_[5];
 struct Job{unsigned slot=0;int64_t rangeFrom=0,rangeTo=0;};struct Reply{String body;int status=200;};
 struct Info{unsigned eventCount=0;uint32_t lastSyncMs=0;bool truncated=false;};
 void* mutex_=nullptr;bool valid=true,permissionMissing_[5]{};AccountCapability missingCapability_[5]{};
 EventRecord* events_[5]{};EventDetailsSnapshot* details_[5]{};Info info_[5]{};uint32_t nextSync_[5]{};
 uint32_t dataRevision_=1;int64_t rangeFrom_[5]{},rangeTo_[5]{};
 void calendarDataChanged(){++dataRevision_;publishLuaEvent(nullptr,"calendar.updated");}
 std::deque<Reply> replies;std::vector<String> urls;uint32_t delay=0;int cancelAt=-1;String message;
 ~CalendarSyncService(){for(unsigned i=0;i<5;++i){free(events_[i]);free(details_[i]);}}
 bool current(const Job&){return valid;}
 void clearAccess(unsigned){}
 void publish(const Job&,const char* value,bool=true){message=value;}
 String detailSnapshotPath(unsigned){return "details";}String snapshotPath(unsigned){return "events";}
 bool request(const char* url,const String&,const String&,Reply& out,uint32_t timeout){
   urls.push_back(url);if(!timeout||timeout>10000||replies.empty())return false;
   out=replies.front();replies.pop_front();now+=delay;
   if(int(urls.size())==cancelAt)valid=false;return true;
 }
 bool fetchCalendar(Job&,EmailProvider,const String&,const String&);
 bool discoverOAuthCalendars(Job&,EmailProvider,const String&);
 std::vector<CloudCalendarCollection> selectedOAuthCalendars(Job&,EmailProvider,const String&){return selected;}
 bool commitCalendarCache(Job&,EventRecord*,EventDetailsSnapshot*,uint8_t,bool);
};
}
