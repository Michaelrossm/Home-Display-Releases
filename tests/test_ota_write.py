from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]


class OtaWriteTests(unittest.TestCase):
    def test_actual_writer_faults_bounds_and_lease(self):
        source = (ROOT / 'src/services/OtaWriteSession.h').read_text()
        source = '\n'.join(line for line in source.splitlines()
                           if not line.startswith('#include') and line != '#pragma once')
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
using esp_err_t=int; using esp_ota_handle_t=unsigned;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_INVALID_ARG=2,
 ESP_ERR_OTA_ROLLBACK_INVALID_STATE=3,ESP_ERR_NO_MEM=4,ESP_ERR_INVALID_SIZE=5;
constexpr unsigned OTA_WITH_SEQUENTIAL_WRITES=0xfffffffe;
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
enum esp_ota_img_states_t {ESP_OTA_IMG_VALID,ESP_OTA_IMG_PENDING_VERIFY};
struct esp_partition_t {unsigned address,size;};
esp_partition_t running{0x10000,10000},target{0x100000,10000};
bool noMemory=false,pending=false;int beginError=0,writeError=0,endError=0;
unsigned begins=0,aborts=0,ends=0,yields=0;void* allocation=nullptr;
std::vector<uint8_t> received;
void* heap_caps_malloc(size_t n,unsigned caps){assert(n==4096&&caps==3);return allocation=noMemory?nullptr:malloc(n);}
void heap_caps_free(void* p){free(p);if(p==allocation)allocation=nullptr;}
const esp_partition_t* esp_ota_get_running_partition(){return &running;}
int esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* s){*s=pending?ESP_OTA_IMG_PENDING_VERIFY:ESP_OTA_IMG_VALID;return 0;}
int esp_ota_begin(const esp_partition_t* p,size_t mode,unsigned* h){assert(p==&target&&mode==OTA_WITH_SEQUENTIAL_WRITES);++begins;if(beginError)return beginError;*h=1;received.clear();return 0;}
int esp_ota_write(unsigned h,const void* p,size_t n){assert(h==1&&p==allocation&&n<=4096);if(writeError)return writeError;auto b=static_cast<const uint8_t*>(p);received.insert(received.end(),b,b+n);return 0;}
int esp_ota_end(unsigned h){assert(h==1);++ends;return endError;}
void esp_ota_abort(unsigned h){assert(h==1);++aborts;}
void vTaskDelay(unsigned n){assert(n==1);++yields;}
namespace hp {struct OtaTrace {enum Phase {None,Begin,Writing,Validating,Readback,Selecting,Ready,Failed};struct Snapshot{unsigned phase=0;};static void record(Phase,unsigned,int=0){}static Snapshot snapshot(){return {};}};}
''' + source + r'''
int main(){
 using hp::OtaWriteSession;
 std::vector<uint8_t> image(9000);for(size_t i=0;i<image.size();++i)image[i]=i%251;
 {OtaWriteSession w;assert(w.acquired());
  // Another thread cannot enter either update path while this lease exists.
  bool locked=false;std::thread thread([&]{OtaWriteSession other;locked=other.acquired();});thread.join();assert(!locked);
  assert(w.begin(&target,image.size())==0);assert(w.begin(&target,1)==ESP_ERR_INVALID_STATE);
  assert(w.write(image.data(),image.size())==0);assert(received==image&&yields==3);
  assert(w.write(image.data(),1)==ESP_ERR_INVALID_ARG);assert(w.finish()==0);
 }assert(aborts==0&&ends==1&&!allocation);
 {OtaWriteSession w;assert(w.begin(&target,10)==0);assert(w.finish()==ESP_ERR_INVALID_SIZE);}assert(aborts==1);
 {OtaWriteSession w;assert(w.begin(&target,10)==0);writeError=99;assert(w.write(image.data(),10)==99);w.abort();w.abort();}assert(aborts==2);writeError=0;
 {OtaWriteSession w;assert(w.begin(&target,10)==0);assert(w.write(image.data(),10)==0);endError=99;assert(w.finish()==99);}assert(aborts==2);endError=0;
 auto before=begins;noMemory=true;{OtaWriteSession w;assert(w.begin(&target,10)==ESP_ERR_NO_MEM);}noMemory=false;assert(begins==before);
 pending=true;{OtaWriteSession w;assert(w.begin(&target,10)==ESP_ERR_OTA_ROLLBACK_INVALID_STATE);}pending=false;assert(begins==before);
 {OtaWriteSession w;assert(w.begin(&running,10)==ESP_ERR_INVALID_ARG);assert(w.begin(&target,10001)==ESP_ERR_INVALID_ARG);}assert(begins==before);
 beginError=99;{OtaWriteSession w;assert(w.begin(&target,10)==99);}assert(!allocation&&aborts==2);
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <thread>'))

    def test_http_security_verification_and_bounded_receive(self):
        code = (ROOT / 'src/services/FileTransferService.cpp').read_text()
        handler = code.split('esp_err_t FileTransferService::firmwareUploadHandler')[1].split('esp_err_t FileTransferService::firmwareRebootHandler')[0]
        self.assertLess(handler.index('writeAuthorized'), handler.index('OtaWriteSession writer'))
        self.assertLess(handler.index('parseFirmwareInfo'), handler.index('writer.begin'))
        self.assertLess(handler.index('parseSha256Header'), handler.index('writer.begin'))
        self.assertLess(handler.index('memcmp(incomingSha'), handler.index('writer.finish'))
        self.assertLess(handler.index('partitionSha256Exact'), handler.index('esp_ota_set_boot_partition'))
        self.assertLess(handler.index('writtenInfo.version != info.version'), handler.index('esp_ota_set_boot_partition'))
        self.assertIn('uploadDeadlineMs = 600000', handler)
        self.assertIn('millis() - lastReceived < 45000U', handler)
        self.assertIn('Transfer-Encoding', handler)
        self.assertNotIn('ps_malloc', handler)
        self.assertNotIn('esp_restart', handler)
        self.assertIn('std::try_to_lock', code.split('firmwareRebootHandler(httpd_req_t* req)')[1])
        web=(ROOT/'src/services/WebServerService.cpp').read_text().split('esp_err_t WebServerService::restartHandler')[1].split('esp_err_t WebServerService::serviceStatusHandler')[0]
        self.assertLess(web.index('std::try_to_lock'),web.index('esp_restart'))

    def test_signed_package_uses_same_writer_without_double_abort(self):
        code = (ROOT / 'src/services/UpdateService.cpp').read_text()
        handler = code.split('bool UpdateService::writeFirmware')[1].split('bool UpdateService::copyPartitionToSd')[0]
        self.assertIn('OtaWriteSession writer', handler)
        self.assertIn('writer.finish()', handler)
        self.assertIn('verifyPartitionRaw', handler)
        self.assertNotIn('esp_ota_abort', handler)
        self.assertNotIn('esp_ota_set_boot_partition', handler)
        self.assertIn('180000U', handler)
        activate=code.split('bool UpdateService::activateFirmware')[1].split('bool UpdateService::restorePartitionFromSd')[0]
        self.assertLess(activate.index('verifyPartitionRaw'),activate.index('esp_ota_set_boot_partition'))
        self.assertIn('std::try_to_lock',activate)

    def test_retained_trace_rejects_corruption_and_contains_no_credentials(self):
        header=(ROOT/'src/services/OtaTrace.h').read_text()
        source=(ROOT/'src/services/OtaTrace.cpp').read_text()
        combined='\n'.join(line for line in (header+'\n'+source).splitlines()
                           if not line.startswith('#include') and line!='#pragma once')
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cstdint>
#include <cassert>
#define RTC_NOINIT_ATTR
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void portENTER_CRITICAL(int*){}void portEXIT_CRITICAL(int*){}
'''+combined+r'''
int main(){using hp::OtaTrace;assert(OtaTrace::snapshot().phase==0);
 OtaTrace::record(OtaTrace::Writing,8192);auto s=OtaTrace::snapshot();assert(s.phase==OtaTrace::Writing&&s.bytes==8192);
 hp::retained.bytes^=1;assert(OtaTrace::snapshot().phase==0);
 OtaTrace::record(OtaTrace::Failed,4096,-7);s=OtaTrace::snapshot();assert(s.phase==OtaTrace::Failed&&s.error==-7);
 hp::retained.phase=99;hp::retained.check=hp::checksum(hp::retained);assert(OtaTrace::snapshot().phase==0);
}
''')
