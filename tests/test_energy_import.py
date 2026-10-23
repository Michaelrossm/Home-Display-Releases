from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class EnergyImportTests(unittest.TestCase):
    def test_native_source_binding_ownership_and_cancellation(self):
        prelude=(ROOT/'tests/native/printer_fake.h').read_text(encoding='utf-8').split('namespace hp {')[0]
        code=(ROOT/'src/apps/power/PowerService.cpp').read_text()
        code=code[code.index('PowerService& PowerService::instance()'):code.index('bool PowerService::begin()')]
        test_system_performance.SystemPerformanceTests().native(prelude+r'''
constexpr int pdTRUE=1;unsigned takes=0;bool available=true;
int xSemaphoreTake(void*,int){++takes;return available;}void xSemaphoreGive(void*){}int pdMS_TO_TICKS(int n){return n;}
namespace hp {
bool energyTrackerCredentials(const String&,unsigned index,String& host,String& password){host="192.0.2.66:80";password="not-exported";return index==1;}
struct SettingsService{struct Snapshot{String powerHost,powerPassword;};static SettingsService& instance(){static SettingsService s;return s;}Snapshot snapshot(){return {};}};
class PowerService {public:static PowerService& instance();bool bindSource(const String&,unsigned);void cleanupOwner(const char*,bool);bool source(String&,String&);
 void* mux_=reinterpret_cast<void*>(1);String sourceOwner_;unsigned sourceIndex_=0;std::atomic<uint32_t> sourceOwnerHash_{0};std::atomic<bool> sourceCancelled_{false},sourceChanged_{false};};
'''+code+r'''
}
int main(){auto& p=hp::PowerService::instance();assert(p.bindSource("strom",1));assert(p.sourceChanged_.exchange(false));
 assert(p.bindSource("strom",1)&&!p.sourceChanged_);assert(!p.bindSource("different-app",1));
 unsigned before=takes;p.cleanupOwner("strom",true);assert(takes==before&&!p.sourceCancelled_);
 p.cleanupOwner("other",false);assert(!p.sourceCancelled_);p.cleanupOwner("strom",false);assert(p.sourceCancelled_&&takes==before);
 String host,password;assert(!p.source(host,password));assert(p.bindSource("different-app",1));assert(p.source(host,password)&&host=="192.0.2.66:80");
}
''',includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_actual_stream_merge_recovery_cancel_and_profiles(self):
        prelude=(ROOT/'tests/native/printer_fake.h').read_text(encoding='utf-8').split('namespace hp {')[0]
        prelude=prelude.replace('void remove(size_t first,size_t count)', 'void remove(size_t first){erase(first);}\n    void remove(size_t first,size_t count)')
        prelude=prelude.replace('int indexOf(char s)const', 'int indexOf(char s,size_t from)const{auto n=find(s,from);return n==npos?-1:int(n);}\n    int indexOf(char s)const')
        source=(ROOT/'src/services/EnergyHistoryImport.cpp').read_text(encoding='utf-8')
        source='\n'.join(x for x in source.splitlines() if not x.startswith('#include'))
        core=(ROOT/'src/services/EnergyCsvCore.h').read_text().replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(prelude+r'''
#include <mutex>
#include <ctime>
constexpr const char* FILE_READ="r",*FILE_WRITE="w";
struct Node {String bytes;};
struct File {std::shared_ptr<Node> node;size_t pos=0;
 explicit operator bool()const{return bool(node);}bool available(){return node&&pos<node->bytes.size();}
 int read(){return available()?static_cast<unsigned char>(node->bytes[pos++]):-1;}
 size_t write(const uint8_t* data,size_t n){if(!node)return 0;node->bytes.append(reinterpret_cast<const char*>(data),n);return n;}
 void close(){node.reset();}void flush(){}time_t getLastWrite(){return 0;}
};
namespace fs {class FS {public:std::map<String,std::shared_ptr<Node>> files;bool failCommit=false;
 File open(const String& name,const char* mode){auto it=files.find(name);if(!strcmp(mode,"w")){auto n=std::make_shared<Node>();files[name]=n;return {n};}return it==files.end()?File{}:File{it->second};}
 bool exists(const String& p){return files.count(p);}
 bool remove(const String& p){files.erase(p);return true;}
 bool rename(const String& a,const String& b){if(!exists(a)||(failCommit&&a.endsWith(".part")))return false;files[b]=files[a];files.erase(a);return true;}
};}
int mbedtls_sha256(const uint8_t* bytes,size_t size,uint8_t* out,int){uint32_t value=0;for(size_t i=0;i<size;++i)value=value*31+bytes[i];for(unsigned i=0;i<32;++i)out[i]=(value>>((i%4)*8))+i;return 0;}
namespace hp {
struct NetworkWorker{static NetworkWorker& instance(){static NetworkWorker w;return w;}void maintain(){}};
void vTaskDelay(int){}
String profiles,credentials="admin:test-secret",response;int httpStatus=200;bool onlineMedia=true,cancelMid=false,removeMid=false,changeSourceMid=false;unsigned requests=0;
String luaStringForNative(const String& owner,const String& name){assert(owner=="strom"&&name=="sources_json");return profiles;}
String luaSecretForNative(const String& owner,const String& name){assert(owner=="strom"&&name.startsWith("meter"));return credentials;}
struct StorageService {struct ArchiveVolume{fs::FS* fs=nullptr;bool usb=true;uint32_t generation=0;};fs::FS disk;
 static StorageService& instance(){static StorageService s;return s;}
 static std::mutex& mutationMutex(){static std::mutex m;return m;}
 bool archiveVolume(ArchiveVolume& v){v.fs=&disk;return onlineMedia;}
 bool archiveVolumeValid(const ArchiveVolume&){return onlineMedia;}
 bool archiveParents(ArchiveVolume&,const String&){return onlineMedia;}
 void notifyMutation(){}
};
struct TimeService {static TimeService& instance(){static TimeService s;return s;}int64_t nowUtc(){return 1770007200;}
 bool localFromEpoch(int64_t t,tm& value){time_t time=t;value=*gmtime(&time);return true;}};
struct DetailRecord{uint32_t timestamp=0;float values[7]{};uint16_t sourceSeconds=60,intervalSeconds=60;};
struct EnergyHistoryService {static EnergyHistoryService& instance(){static EnergyHistoryService s;return s;}String query(const String&,size_t){return "[]";}
 template<class Visitor>bool visitDetail(uint32_t,uint32_t,Visitor){return false;}};
struct LuaJobService {
 struct Result{bool ok=false;int status=0;String body,error;uint64_t size=0;};
 struct Request{String owner,url,basicSecret,body;unsigned resource=1,kind=5,method=0,streamMode=0,timeout=0,deadlineMs=0,permissions=0;
  const std::atomic<bool>* cancelled=nullptr;bool(*nativeWrite)(void*,const uint8_t*,size_t,String&)=nullptr;void* nativeWriteContext=nullptr;};
 static bool resolveHttpTarget(const Request& q,String& pinned,String& hostname){pinned=q.url;hostname="192.0.2.66";return true;}
 static void executeHttp(const Request& q,Result& r){assert(q.streamMode==1&&q.timeout==2500&&q.deadlineMs==120000&&q.owner=="strom");
  assert(q.url=="http://192.0.2.66/api/v1/history.csv?range=complete"&&q.basicSecret=="meter1_login");++requests;r.status=httpStatus;
  if(httpStatus!=200){r.error="authentication";return;}
  for(size_t i=0;i<response.size();i+=7){if(cancelMid&&i>80)const_cast<std::atomic<bool>*>(q.cancelled)->store(true);if(removeMid&&i>80)onlineMedia=false;
   if(!q.nativeWrite(q.nativeWriteContext,reinterpret_cast<const uint8_t*>(response.data()+i),std::min<size_t>(7,response.size()-i),r.error))return;}
  if(changeSourceMid)profiles.replace("192.0.2.66","192.0.2.77");r.ok=true;
 }
};
enum class AppPermission{Network=1};unsigned perm(AppPermission p){return unsigned(p);}
}
'''+core+'\n'+source+r'''
int main(){using namespace hp;
 profiles=R"([{"enabled":true,"type":"irtracker","role":"grid","host":"192.0.2.66"}])";
 Source source;assert(sourceFor("strom",1,source));auto path=source.path;
 credentials="admin:rotated";assert(sourceFor("strom",1,source)&&source.path==path);
 credentials="different:secret";assert(sourceFor("strom",1,source)&&source.path!=path);credentials="admin:test-secret";
 auto& fs=StorageService::instance().disk;
 response=String(energyCsvHeader)+"\n1770000000,123,100,140,1,0,60\n1770000060,124,100,140,1,0,60\n";
 std::atomic<bool> cancelled{false};LuaJobService::Request q;q.owner="strom";q.cancelled=&cancelled;LuaJobService::Result r;
 executeEnergyHistorySync(q,r);assert(r.ok&&fs.exists(path));const String fine=fs.files[path]->bytes;assert(fine.find("1770000060,124")!=String::npos);
 response=String(energyCsvHeader)+"\n1770000000,999,999,999,1,0,3600\n";
 r={};executeEnergyHistorySync(q,r);assert(r.ok&&fs.files[path]->bytes==fine); // Compaction cannot erase old minutes.
 httpStatus=401;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&fs.files[path]->bytes==fine);httpStatus=200;
 response=String(energyCsvHeader)+"\n1770000120,125,100,150,1,0,60\n";
 cancelMid=true;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&fs.files[path]->bytes==fine&&!fs.exists(path+".part"));cancelMid=false;cancelled=false;
 removeMid=true;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&fs.files[path]->bytes==fine);removeMid=false;onlineMedia=true;
 fs.failCommit=true;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&fs.files[path]->bytes==fine);fs.failCommit=false;
 changeSourceMid=true;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&r.error=="archive_source_changed"&&fs.files[path]->bytes==fine);changeSourceMid=false;profiles.replace("192.0.2.77","192.0.2.66");
 assert(fs.rename(path,path+".bak"));r={};executeEnergyHistorySync(q,r);assert(r.ok&&fs.exists(path)&&!fs.exists(path+".bak"));
 const String complete=fs.files[path]->bytes;
 response=String(energyCsvHeader)+"\n1770000060,4,3,5,0,0,60\n1770000000,2,1,3,0,0,60\n";
 r={};executeEnergyHistorySync(q,r);assert(!r.ok&&fs.files[path]->bytes==complete);
 onlineMedia=false;r={};executeEnergyHistorySync(q,r);assert(!r.ok&&r.error=="external_storage_required");onlineMedia=true;
 EnergyCsvRecord parsed;assert(!parseEnergyCsv("1770000000,nan,1,2,0,0,60",parsed));assert(!parseEnergyCsv("1770000000,1,1,2,0,0,0",parsed));
 q.url="today";r={};executeEnergyHistoryQuery(q,r);assert(r.ok&&r.body.find("gridPower")!=String::npos);
}
''',includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_owner_queue_and_no_extra_tasks(self):
        bridge=(ROOT/'src/apps/lua/LuaEnergyBridge.cpp').read_text()
        self.assertIn('"hp_energy_sync_history"',bridge)
        self.assertIn('request.owner=app->id()',bridge)
        self.assertIn('Request::Lane::Transfer',bridge)
        self.assertIn('Request::Lane::LocalTransfer',bridge)
        source=(ROOT/'src/services/EnergyHistoryImport.cpp').read_text()
        self.assertIn('LuaJobService::executeHttp(http,result)',source)
        self.assertNotIn('xTaskCreate',source)
        self.assertNotIn('lv_',source)
