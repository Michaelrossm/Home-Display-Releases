from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def stripped(path):return '\n'.join(l for l in read(path).splitlines() if not l.startswith('#include') and l!='#pragma once')

class PerformanceBottleneckTests(unittest.TestCase):
    def native(self,code):test_system_performance.SystemPerformanceTests().native(code)

    def test_real_worker_local_work_does_not_wake_radio(self):
        code=read('tests/native/network_v3_fake.h')+stripped('src/core/LatencyHistogram.h')
        code+=stripped('src/services/NetworkWorker.h').replace('private:','public:')+stripped('src/services/NetworkWorker.cpp')
        self.native(code+r'''
unsigned calls=0;
void local(void*){++calls;assert(!hp::NetworkWorker::instance().radioBurstActive());now+=10;}
void network(void*){++calls;assert(hp::NetworkWorker::instance().radioBurstActive());now+=100;}
int main(){auto& w=hp::NetworkWorker::instance();assert(w.begin());
assert(w.submitLocal(local,nullptr));assert(w.submitLocalTransfer(local,nullptr));assert(!w.radioBurstActive());
try{w.run();}catch(const Drained&){}
hp::NetworkWorker::Job j;assert(xQueueReceive(w.transferQ_,&j,0));w.executeJob(j,true);assert(calls==2&&!w.radioBurstActive());
assert(w.submit(network,nullptr));assert(w.radioBurstActive());try{w.run();}catch(const Drained&){}
assert(w.networkQueued_==0&&w.networkTransfers_==0&&w.radioBurstActive());now+=5001;assert(!w.radioBurstActive());
assert(w.submitLocal(local,nullptr));try{w.run();}catch(const Drained&){}assert(!w.radioBurstActive());
for(int i=0;i<8;++i)assert(w.submit(network,nullptr));assert(!w.submit(network,nullptr));assert(w.networkQueued_==8);
try{w.run();}catch(const Drained&){}assert(w.networkQueued_==0&&w.networkTransfers_==0);
}
''')

    def test_long_requests_isolated_but_commands_not_replayed(self):
        source=read('src/apps/lua/LuaJobService.cpp')
        transfer='const bool forcedTransfer='+source.split('const bool forcedTransfer=',1)[1].split('const bool queued=',1)[0]
        self.native(r'''
#include <cassert>
#include <string>
struct Request{enum class Lane{Automatic,LocalTransfer,Transfer};Lane lane=Lane::Automatic;int kind=0,streamMode=0,timeout=5000;std::string fileName,completion;};
int lane(Request request){'''+transfer+r'''
return transfer?2:urgent?1:0;}
int main(){Request r;assert(lane(r)==2);r.timeout=500;assert(lane(r)==0);
r.streamMode=1;assert(lane(r)==2);r.kind=2;assert(lane(r)==2);
r.kind=5;r.streamMode=0;r.fileName="preview_file";assert(lane(r)==2);
r.fileName="pause";r.completion="printer.changed";assert(lane(r)==1);
r.fileName="upload_file";assert(lane(r)==2);
r.fileName="storage";r.completion="storage.completed";r.lane=Request::Lane::LocalTransfer;assert(lane(r)==2);}
''')
        sync=read('src/services/CalendarSyncService.cpp').split('bool CalendarSyncService::enqueue',1)[1].split('void CalendarSyncService::run',1)[0]
        self.assertNotIn('submitPriority',sync)
        self.assertIn('submitLocal',sync)
        self.assertIn('submitTransfer',sync)

    def test_runtime_theme_refresh_has_no_file_or_display_lock(self):
        source=read('src/ui/ThemeCatalog.cpp')
        refresh=source.split('void ThemeCatalog::refreshSd()',1)[1].split('void ThemeCatalog::scanWorker',1)[0]
        worker=source.split('void ThemeCatalog::scanWorker',1)[1].split('void ThemeCatalog::scanSd()',1)[0]
        self.assertIn('submitLocalTransfer',refresh)
        self.assertNotIn('openSdDirectory',refresh)
        self.assertNotIn('loadThemeFile',refresh)
        self.assertNotIn('DisplayUiGuard',worker)
        self.assertIn('resultReady_.store(true',worker)
        main=read('src/main.cpp')
        self.assertLess(main.index('ThemeCatalog::instance().begin()'),main.index('DisplayManager::instance().begin()'))

    def test_real_backlight_off_retains_scanout_clock_and_wake_is_safe(self):
        source=read('src/hardware/DisplayManager.cpp')
        setter=source.split('void DisplayManager::setBrightness',1)[1].split('bool DisplayManager::lockUi',1)[0]
        self.native(r'''
#include <cassert>
#include <cstdint>
#include <vector>
#define HOMEDISPLAY_USE_WAVESHARE_BSP 1
#define HOMEPANEL_FEATURE_POWER_MANAGEMENT 1
constexpr int ESP_OK=0;std::vector<int> order;bool offFail=false,onFail=false;
int bsp_display_backlight_off(){order.push_back(0);return offFail?1:0;}
int bsp_display_backlight_on(){order.push_back(2);return onFail?1:0;}
int bsp_display_brightness_set(int){order.push_back(3);return 0;}
struct PowerManager{int held=1;bool fail=false;static PowerManager& instance(){static PowerManager p;return p;}bool acquirePerformance(){order.push_back(1);if(fail)return false;++held;return true;}void releasePerformance(){order.push_back(4);--held;}};
struct DisplayManager{bool initialized_=true,performanceHeld_=true;int lastHardwarePercent_=100;uint8_t brightness_=255;void setBrightness(uint8_t);};
void DisplayManager::setBrightness'''+setter+r'''
int main(){DisplayManager d;auto& p=PowerManager::instance();d.setBrightness(0);assert(p.held==1&&d.performanceHeld_&&d.brightness_==0);
assert((order==std::vector<int>{0}));order.clear();d.setBrightness(0);assert(order.empty());
d.setBrightness(255);assert((order==std::vector<int>{2,3}));assert(p.held==1&&d.performanceHeld_);
offFail=true;d.setBrightness(0);assert(p.held==1&&d.brightness_==255);offFail=false;d.setBrightness(0);
for(int i=0;i<100;++i){d.setBrightness(255);d.setBrightness(0);assert(p.held==1&&d.performanceHeld_);}
// Defensively cover a display without its lock: acquire before backlight-on.
d.performanceHeld_=false;p.held=0;p.fail=true;d.setBrightness(255);assert(p.held==0&&d.brightness_==0);
p.fail=false;order.clear();d.setBrightness(255);assert((order==std::vector<int>{1,2,3}));assert(p.held==1);
d.setBrightness(0);onFail=true;d.setBrightness(255);assert(p.held==1&&d.brightness_==0);onFail=false;d.setBrightness(255);assert(p.held==1);}
''')
        self.assertNotIn('PowerManager::instance().acquirePerformance()',read('src/main.cpp'))

    def test_slow_log_diagnostics_do_not_feed_persistence_and_worker_blocks(self):
        source=read('src/core/BackgroundScheduler.cpp')
        worker=source.split('void BackgroundScheduler::runWorker()',1)[1].split('UBaseType_t BackgroundScheduler::workerStackHighWater',1)[0]
        self.native(r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <stdexcept>
using String=std::string;
String StringNumber(unsigned n){return std::to_string(n);}
String StringNumber(const char* text){return text;}
#define String(x) StringNumber(x)
#define pdMS_TO_TICKS(x) (x)
constexpr bool pdTRUE=true;
uint64_t clockUs=0;unsigned writes=0,warnings=0,turns=0,delays=0;
uint64_t esp_timer_get_time(){return clockUs;}
void taskYIELD(){assert(false&&"busy worker must block, not merely yield");}
void vTaskDelay(unsigned ticks){assert(ticks>=1);++delays;}
bool xQueueReceive(void*,void*,unsigned){if(turns++<20)return true;throw std::runtime_error("done");}
void updateMax(uint32_t& target,uint32_t value){if(value>target)target=value;}
struct IBackgroundJob{void backgroundTick(){++writes;clockUs+=3704000;}
 const char* backgroundName(){return "event-log";}};
struct EventLog{static EventLog& instance(){static EventLog log;return log;}
 void add(const char*,const char*,const String&){assert(false&&"recursive durable warning");}
 void addTransient(const char*,const char*,const String&){++warnings;}};
struct BackgroundScheduler{struct WorkItem{unsigned index=0;};unsigned n_=1;
 IBackgroundJob job;IBackgroundJob* jobs_[1]={&job};void* blockingQueue_=nullptr;
 uint32_t maxBlockingUs_=0,completedBlocking_=0;void clearInFlight(unsigned){}void runWorker();};
void BackgroundScheduler::runWorker()'''+worker+r'''
int main(){BackgroundScheduler s;try{s.runWorker();}catch(const std::runtime_error&){}
 assert(writes==20&&warnings==20&&delays==20&&s.completedBlocking_==20);
 assert(s.maxBlockingUs_==3704000);}
''')

if __name__=='__main__':unittest.main()
