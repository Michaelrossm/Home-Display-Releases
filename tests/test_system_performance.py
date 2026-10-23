import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def read(path):return (ROOT/path).read_text(encoding='utf-8')
def stripped(path):return '\n'.join(line for line in read(path).splitlines() if not line.startswith('#include') and line!='#pragma once').replace('__attribute__((packed))','')

class SystemPerformanceTests(unittest.TestCase):
    def native(self,code,includes=()):
        compiler=shutil.which('g++') or shutil.which('clang++')
        with tempfile.TemporaryDirectory(prefix='hp-system-perf-') as directory:
            path=Path(directory);(path/'check.cpp').write_text(code,encoding='utf-8')
            if compiler:command=[compiler,'-std=c++17',*[f'-I{p}' for p in includes],'check.cpp','-o','check.exe']
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists():self.skipTest('C++ compiler unavailable')
                flags=' '.join(f'/I"{p}"' for p in includes)
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 {flags} check.cpp /Fe:check.exe"'
            result=subprocess.run(command,cwd=path,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(path/'check.exe')],capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def string_fixture(self):
        fixture=read('tests/native/printer_fake.h').split('uint32_t now=')[0]
        return fixture.replace('void remove(size_t first,size_t count)', 'void remove(size_t first){erase(first);}\n    void remove(size_t first,size_t count)')

    def test_actual_settings_cache_invalidates_without_repeated_nvs_reads(self):
        fixture=self.string_fixture()+r'''
namespace hp {
struct SecurityWriteGuard{};
struct SecurityService {static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return true;}bool tabletLocked(){return false;}};
struct FileBrowserService {static void notifyWallpaperChanged(){}};
struct ConfigService {
 unsigned reads=0,writes=0,rev=0;std::map<String,String> values;
 static ConfigService& instance(){static ConfigService s;return s;}
 bool begin(){return true;}bool writable(){return true;}unsigned revision(){return rev;}
 String getString(const char* key,const char* fallback=""){++reads;auto it=values.find(key);return it==values.end()?String(fallback):it->second;}
 String getSecretString(const char* key,const char* fallback=""){return getString(key,fallback);}
 unsigned getU8(const char* key,unsigned fallback=0){return number(key,fallback);}
 unsigned getU16(const char* key,unsigned fallback=0){return number(key,fallback);}
 bool getBool(const char* key,bool fallback=false){return number(key,fallback)!=0;}
 unsigned number(const char* key,unsigned fallback){++reads;auto it=values.find(key);return it==values.end()?fallback:unsigned(std::stoi(it->second));}
 bool putString(const char* key,const String& v){auto it=values.find(key);if(it!=values.end()&&it->second==v)return true;values[key]=v;++writes;++rev;return true;}
 bool putSecretString(const char* key,const String& v){return putString(key,v);}
 bool putBool(const char* key,bool v){return putString(key,v?"1":"0");}
 bool putU8(const char* key,unsigned v){return putString(key,String(v));}
 bool putU16(const char* key,unsigned v){return putString(key,String(v));}
};
}
'''
        tail=r'''
int main(){using namespace hp;auto& service=SettingsService::instance();auto& config=ConfigService::instance();assert(service.begin());
 auto first=service.snapshot();const auto loaded=config.reads;assert(loaded>30);
 for(unsigned i=0;i<100;++i)assert(service.snapshot().brightness==first.brightness);
 assert(config.reads==loaded);assert(service.setBrightness(77));assert(service.snapshot().brightness==77&&config.reads>loaded);
 const auto second=config.reads;service.snapshot();assert(config.reads==second);
 config.putString("weather","Berlin");assert(service.snapshot().weatherRegion=="Berlin");
 config.putSecretString("power_pwd","changed");assert(service.snapshot().powerPassword=="changed");
 auto copy=service.snapshot();copy.hostname="mutated-copy";assert(service.snapshot().hostname!="mutated-copy");
 service.save(service.snapshot());auto saved=service.snapshot();auto writes=config.writes;
 service.save(saved);assert(config.writes==writes);service.snapshot();auto reads=config.reads;service.snapshot();assert(config.reads==reads);
}
'''
        self.native(fixture+stripped('src/services/SettingsService.h')+stripped('src/services/SettingsService.cpp')+tail)

    def test_actual_event_log_runtime_queue_is_bounded_and_worker_only(self):
        fixture=self.string_fixture()+r'''
#include <ctime>
#include <deque>
#include <functional>
using TickType_t=unsigned;using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
unsigned critical=0;
void portENTER_CRITICAL(int*){++critical;}void portEXIT_CRITICAL(int*){--critical;}
struct Queue {unsigned capacity,size;std::deque<std::vector<uint8_t>> values;};using QueueHandle_t=Queue*;
Queue* xQueueCreate(unsigned n,unsigned bytes){return new Queue{n,bytes,{}};}
void vQueueDelete(Queue* q){delete q;}void xQueueReset(Queue* q){q->values.clear();}
constexpr int pdTRUE=1;constexpr int pdFALSE=0;
int xQueueSend(Queue* q,const void* data,unsigned wait){assert(wait==0);if(q->values.size()==q->capacity)return pdFALSE;const auto* p=static_cast<const uint8_t*>(data);q->values.emplace_back(p,p+q->size);return pdTRUE;}
int xQueueReceive(Queue* q,void* data,unsigned wait){assert(wait==0);if(q->values.empty())return pdFALSE;memcpy(data,q->values.front().data(),q->size);q->values.pop_front();return pdTRUE;}
using SemaphoreHandle_t=bool*;bool* xSemaphoreCreateMutex(){return new bool(false);}
constexpr unsigned portMAX_DELAY=0xffffffff;
int xSemaphoreTake(bool* m,unsigned){if(*m)return pdFALSE;*m=true;return pdTRUE;}void xSemaphoreGive(bool* m){*m=false;}
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;void* heap_caps_calloc(size_t n,size_t s,unsigned){return calloc(n,s);}
uint32_t millis(){return 1234;}
size_t strlcpy(char* out,const char* in,size_t size){size_t n=strlen(in);if(size){memcpy(out,in,std::min(n,size-1));out[std::min(n,size-1)]=0;}return n;}
constexpr const char* FILE_WRITE="w";constexpr const char* FILE_READ="r";constexpr int SeekSet=0;
unsigned diskWrites=0;bool failWrites=false;std::vector<uint8_t> disk;
struct File {bool valid=false;size_t at=0;explicit operator bool()const{return valid;}size_t size(){return disk.size();}
 bool seek(size_t offset,int){at=offset;return true;}size_t read(uint8_t* out,size_t n){n=std::min(n,disk.size()-std::min(at,disk.size()));memcpy(out,disk.data()+at,n);at+=n;return n;}
 size_t write(const uint8_t* data,size_t n){++diskWrites;if(failWrites)return 0;if(disk.size()<at+n)disk.resize(at+n);memcpy(disk.data()+at,data,n);at+=n;return n;}void flush(){}void close(){valid=false;}};
struct FS {File open(const String&,const char* mode){if(!strcmp(mode,"w")){disk.clear();return {true,0};}return {!disk.empty(),0};}void remove(const String&){disk.clear();}};
namespace hp {
enum class BackgroundJobClass{Quick,Blocking};struct IBackgroundJob{virtual ~IBackgroundJob()=default;virtual void backgroundTick()=0;virtual BackgroundJobClass backgroundClass()const=0;virtual uint32_t backgroundIntervalMs()const=0;virtual const char* backgroundName()const=0;};
struct BackgroundScheduler {IBackgroundJob* job=nullptr;static BackgroundScheduler& instance(){static BackgroundScheduler s;return s;}bool add(IBackgroundJob* j){job=j;return true;}};
enum class StorageArea{Recovery};struct StorageService {FS fs;static StorageService& instance(){static StorageService s;return s;}FS* fsFor(StorageArea){return &fs;}String pathFor(StorageArea,const char* p){return p;}void ensureParentDirs(StorageArea,const char*){}};
}
'''
        tail=r'''
int main(){using namespace hp;auto& log=EventLog::instance();assert(log.begin(true));assert(log.add("INFO","BOOT","start"));unsigned bootWrites=diskWrites;
 assert(log.deferPersistence()&&BackgroundScheduler::instance().job==&log);
 for(unsigned i=0;i<16;++i)assert(log.add("WARN","TEST","runtime"));
 assert(diskWrites==bootWrites&&!log.add("WARN","FULL","still-in-ram")&&log.persistenceDrops()==1);
 assert(log.count()==18&&log.warningCount()==17);assert(log.addTransient("INFO","TRANSIENT","no disk"));
 log.forEach([&](const EventLog::Record&){assert(critical==0);return false;});
 log.backgroundTick();assert(diskWrites==bootWrites+4);
 failWrites=true;log.backgroundTick();assert(log.persistenceFailures()==2);failWrites=false;
 assert(log.clear()&&log.count()==0);unsigned afterClear=diskWrites;log.backgroundTick();assert(diskWrites==afterClear);
 assert(log.setPersistence(false));assert(log.add("ERROR","RAM","only")&&diskWrites==afterClear&&log.errorCount()==1);
}
'''
        self.native(fixture+stripped('src/core/EventLog.h')+stripped('src/core/EventLog.cpp')+tail)

    def test_main_loop_paths_do_not_wait_for_cloud_or_server_shutdown(self):
        calendar=read('src/services/CalendarSyncService.cpp').split('void CalendarSyncService::backgroundTick(){',1)[1].split('void CalendarSyncService::execute',1)[0]
        self.assertIn('Guard lock(mutex_,0);if(!lock)return',calendar)
        events=read('src/services/CloudAccountApi.cpp').split('void CalendarSyncService::publishAccountEvents()',1)[1]
        self.assertIn('Lock lock(mutex_,0);if(!lock)return',events)
        transfer=read('src/services/FileTransferService.cpp').split('void FileTransferService::loop()',1)[1].split('void FileTransferService::stopExpiredSession',1)[0]
        self.assertIn('submit(stopExpiredSession',transfer);self.assertNotIn('stop();',transfer)
        logger=read('src/core/EventLog.cpp');self.assertNotIn('xTaskCreate',logger)
        self.assertIn('i<2&&xQueueReceive',logger)
        self.assertIn('nextScanMs_=millis()+250',read('src/apps/settings/DynamicAppSettings.cpp'))

    def test_actual_config_writes_skip_unchanged_values_and_keep_failure_semantics(self):
        fixture=self.string_fixture()+r'''
enum {PT_INVALID,PT_STR,PT_U8,PT_U16,PT_U32};
struct Preferences {
 std::map<String,std::pair<int,String>> data;unsigned writes=0;bool fail=false;
 bool isKey(const char* k){return data.count(k)!=0;}int getType(const char* k){auto it=data.find(k);return it==data.end()?PT_INVALID:it->second.first;}
 String getString(const char* k){auto it=data.find(k);return it==data.end()?String():it->second.second;}
 unsigned getUChar(const char* k){return number(k);}unsigned getUShort(const char* k){return number(k);}unsigned getUInt(const char* k){return number(k);}
 unsigned number(const char* k){auto v=getString(k);return v.empty()?0:unsigned(std::stoi(v));}
 size_t store(const char* k,int type,const String& value,size_t bytes){++writes;if(fail)return 0;data[k]={type,value};return bytes;}
 size_t putString(const char* k,const String& v){return store(k,PT_STR,v,v.size()+1);}
 size_t putUChar(const char* k,uint8_t v){return store(k,PT_U8,String(unsigned(v)),sizeof(v));}
 size_t putUShort(const char* k,uint16_t v){return store(k,PT_U16,String(unsigned(v)),sizeof(v));}
 size_t putUInt(const char* k,uint32_t v){return store(k,PT_U32,String(unsigned(v)),sizeof(v));}
 size_t putBool(const char* k,bool v){return putUChar(k,v?1:0);}bool remove(const char* k){++writes;if(fail)return false;data.erase(k);return true;}
};
namespace hp {struct ConfigService {
 Preferences prefs_,secret;bool writeEnabled=true,secretsOpen_=true;unsigned rev=0;
 bool writable(){return writeEnabled;}Preferences& secrets(){return secret;}bool changed(bool ok){if(ok)++rev;return ok;}
 uint32_t getU32(const char* k,uint32_t fallback){return prefs_.isKey(k)?prefs_.getUInt(k):fallback;}
 static bool validNvsKey(const char* k){return k&&k[0]&&strlen(k)<=15;}
 bool putString(const char*,const String&);bool putU8(const char*,uint8_t);bool putU16(const char*,uint16_t);bool putU32(const char*,uint32_t);bool putBool(const char*,bool);bool remove(const char*);
 bool putSecretString(const char*,const String&);bool removeSecret(const char*);
};}
'''
        config=read('src/services/ConfigService.cpp')
        methods=config[config.index('bool ConfigService::putString'):config.index('bool ConfigService::clearKeys')]
        secrets=config[config.index('bool ConfigService::putSecretString'):config.rindex('\n}')]
        tail=r'''
int main(){hp::ConfigService c;
 assert(c.putString("host","display")&&c.putU8("brightness",77)&&c.putU16("n_da",40)&&c.putU32("boot_count",1)&&c.putBool("net_wps",true));
 const auto writes=c.prefs_.writes,revision=c.rev;
 assert(c.putString("host","display")&&c.putU8("brightness",77)&&c.putU16("n_da",40)&&c.putU32("boot_count",1)&&c.putBool("net_wps",true));
 assert(c.prefs_.writes==writes&&c.rev==revision);
 c.prefs_.fail=true;assert(!c.putU8("brightness",78)&&c.rev==revision);assert(c.putU8("brightness",77));c.prefs_.fail=false;
 assert(c.putSecretString("power_pwd","secret"));auto secretWrites=c.secret.writes;assert(c.putSecretString("power_pwd","secret")&&c.secret.writes==secretWrites);
 c.secret.fail=true;auto before=c.rev;assert(!c.putSecretString("power_pwd","new")&&c.rev==before);c.secret.fail=false;
 assert(!c.putU8("this_key_is_too_long",1));c.writeEnabled=false;assert(!c.putU8("brightness",77));c.writeEnabled=true;
 assert(c.remove("missing")&&c.rev==before);assert(c.remove("brightness")&&c.rev>before);
 assert(c.putString("typed_zero","0"));auto typed=c.prefs_.writes;assert(c.putU8("typed_zero",0)&&c.prefs_.writes==typed+1);
 assert(c.putSecretString("web_admin_pwd","first"));auto generation=c.getU32("web_auth_rev",0);assert(generation==1);
 assert(c.putSecretString("web_admin_pwd","first")&&c.getU32("web_auth_rev",0)==generation);
 assert(c.putSecretString("web_admin_pwd","second")&&c.putSecretString("web_admin_pwd","first")&&c.getU32("web_auth_rev",0)==generation+2);
 c.prefs_.fail=true;assert(!c.putSecretString("web_admin_pwd","third")&&c.secret.getString("web_admin_pwd")=="first");c.prefs_.fail=false;
 assert(c.removeSecret("web_admin_pwd")&&c.getU32("web_auth_rev",0)==generation+3);
}
'''
        self.native(fixture+'namespace hp {\n'+methods+secrets+'\n}\n'+tail)

    def test_actual_web_expiration_is_deferred_and_rechecks_extended_sessions(self):
        fixture=self.string_fixture()+r'''
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
namespace hp {
struct NetworkWorker {using Fn=void(*)(void*);Fn fn=nullptr;void* context=nullptr;bool reject=false;unsigned submits=0;
 static NetworkWorker& instance(){static NetworkWorker w;return w;}bool submit(Fn f,void* p,const char*){++submits;if(reject)return false;fn=f;context=p;return true;}
 void execute(){auto f=fn;fn=nullptr;assert(f);f(context);}};
struct EventLog {unsigned events=0;static EventLog& instance(){static EventLog e;return e;}void add(const char*,const char*,const char*){++events;}};
struct FileTransferService {bool active=true;unsigned stops=0;std::atomic<uint32_t> expiresAtMs_{200};std::atomic<bool> timedSession_{true},expirationQueued_{false};uint32_t lastExpirationAttempt_=0;
 bool running(){return active;}void stop(){active=false;timedSession_=false;++stops;}void loop();static void stopExpiredSession(void*);};
}
'''
        source=read('src/services/FileTransferService.cpp')
        methods=source[source.index('void FileTransferService::loop()'):source.index('size_t FileTransferService::activeClients()')]
        tail=r'''
int main(){using namespace hp;FileTransferService service;auto& worker=NetworkWorker::instance();
 service.loop();assert(!worker.fn&&service.stops==0);clockMs=200;service.loop();assert(worker.fn&&service.stops==0&&service.timedSession_);
 service.expiresAtMs_=500;worker.execute();assert(service.stops==0&&!service.expirationQueued_);
 clockMs=500;service.loop();auto f=worker.fn;worker.execute();assert(service.stops==1&&EventLog::instance().events==1);f(&service);assert(service.stops==1);
 service.active=true;service.timedSession_=true;service.expiresAtMs_=800;clockMs=800;worker.reject=true;service.loop();assert(!service.expirationQueued_);
 auto attempts=worker.submits;clockMs=801;service.loop();assert(worker.submits==attempts);
 worker.reject=false;clockMs=1051;service.loop();worker.execute();assert(service.stops==2);
}
'''
        self.native(fixture+'namespace hp {\n'+methods+'\n}\n'+tail)

if __name__=='__main__':unittest.main()
