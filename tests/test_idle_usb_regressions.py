from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

def stripped(path):
    return "\n".join(line for line in read(path).splitlines()
                     if not line.startswith("#include") and line != "#pragma once")

class IdleUsbRegressions(unittest.TestCase):
    def native(self, code):
        with tempfile.TemporaryDirectory(prefix="hp-idle-usb-") as temp:
            folder = Path(temp)
            (folder / "check.cpp").write_text(code, encoding="utf-8")
            compiler = shutil.which("g++") or shutil.which("clang++")
            if compiler:
                command = [compiler, "-std=c++20", "check.cpp", "-o", "check.exe"]
            else:
                setup = Path("C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
                if not setup.exists():
                    self.skipTest("Native compiler unavailable")
                command = f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++20 check.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(folder / "check.exe")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_idle_touch_after_early_clock_and_counter_wrap(self):
        fake = r'''
#include <atomic>
#include <cstdint>
#include <cassert>
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
namespace hp {
struct IdleProfile {uint32_t dimAfter=1,saverAfter=2,offAfter=3;bool saverEnabled=true;};
struct Settings {IdleProfile normal,media,power;};
bool injectTouch=false;
void inject();
struct SettingsService {static SettingsService& instance(){static SettingsService s;return s;}
 Settings snapshot(){if(injectTouch){injectTouch=false;inject();}return {};}};
struct PowerManager {static PowerManager& instance(){static PowerManager s;return s;}void requestBurst(int){}};
struct SecurityService {static SecurityService& instance(){static SecurityService s;return s;}bool lockAfterDisplayOff(){return false;}void lockTablet(){}bool tabletLocked(){return false;}};
enum class EventType{IdleStateChanged};struct Event {EventType type;void* data;};
unsigned transitions=0;
struct EventBus {static EventBus& instance(){static EventBus s;return s;}void publish(Event){++transitions;}};
}
'''
        body = r'''
namespace hp {void inject(){++clockMs;IdleService::instance().activity();}}
int main(){using namespace hp;auto& idle=IdleService::instance();idle.begin();
 clockMs=2000;injectTouch=true;idle.loop();assert(idle.stage()==IdleStage::Active&&transitions==0);
 for(int i=0;i<100;++i){clockMs+=100;idle.activity();idle.loop();assert(idle.stage()==IdleStage::Active);}
 clockMs+=2100;idle.loop();assert(idle.stage()==IdleStage::Saver);
 idle.activity();assert(idle.stage()==IdleStage::Active);
 clockMs=UINT32_MAX-100;idle.activity();idle.loop();clockMs=200;idle.loop();assert(idle.stage()==IdleStage::Active);
 clockMs=2100;idle.loop();assert(idle.stage()==IdleStage::Saver);
 clockMs=5000;idle.keepAwake(IdleLease::PhotoFrame,1000);idle.loop();assert(idle.stage()==IdleStage::Active);
 clockMs=6100;idle.loop();assert(idle.stage()==IdleStage::Off);
 clockMs=UINT32_MAX-100;idle.keepAwake(IdleLease::PhotoFrame,500);idle.loop();assert(idle.stage()==IdleStage::Active);
 clockMs=200;idle.loop();assert(idle.stage()==IdleStage::Active);
 clockMs=2000;idle.loop();assert(idle.stage()!=IdleStage::Active);
}
'''
        self.native(fake + stripped("src/services/IdleService.h") + stripped("src/services/IdleService.cpp") + body)

    def test_usb_format_runs_on_existing_manager_with_identity_and_security_checks(self):
        fake = r'''
#include <atomic>
#include <cstdint>
#include <cassert>
using esp_err_t=int;constexpr int ESP_OK=0,pdTRUE=1,portMAX_DELAY=-1;
using QueueHandle_t=void*;using SemaphoreHandle_t=void*;
using msc_host_device_handle_t=void*;using msc_host_vfs_handle_t=void*;
struct msc_host_event_t{};
struct esp_vfs_fat_mount_config_t {bool format_if_mount_failed;int max_files;int allocation_unit_size;};
bool mutexHeld=false,queueAccept=true;unsigned formats=0,registrations=0;
uint32_t clockMs=0;uint32_t millis(){return clockMs;}int pdMS_TO_TICKS(int ms){return ms;}void vTaskDelay(int ms){clockMs+=ms;}
int xSemaphoreTakeRecursive(void*,int){assert(!mutexHeld);mutexHeld=true;return pdTRUE;}
void xSemaphoreGiveRecursive(void*){assert(mutexHeld);mutexHeld=false;}
int xQueueSend(void*,const void*,int){return queueAccept?pdTRUE:0;}
int msc_host_vfs_format(void*,const esp_vfs_fat_mount_config_t* c,void*){assert(mutexHeld&&!c->format_if_mount_failed);++formats;return ESP_OK;}
int msc_host_vfs_unregister(void*){assert(mutexHeld);return ESP_OK;}
int msc_host_vfs_register(void*,const char*,const esp_vfs_fat_mount_config_t*,void** v){assert(mutexHeld);*v=(void*)3;++registrations;return ESP_OK;}
int esp_vfs_fat_info(const char*,uint64_t* total,uint64_t* free){assert(mutexHeld);*total=100;*free=25;return ESP_OK;}
namespace hp {bool allowed=true,locked=false;
struct SecurityService {static SecurityService& instance(){static SecurityService s;return s;}
 bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};}
'''
        header = stripped("src/services/UsbStorageService.h").replace("private:", "public:")
        methods = read("src/services/UsbStorageService.cpp").split("bool UsbStorageService::acquireFiles()", 1)[1]
        body = r'''
int main(){using namespace hp;auto& usb=UsbStorageService::instance();usb.started_=true;usb.queue_=(void*)1;
 usb.filesMutex_=(void*)1;usb.device_=(void*)2;usb.vfs_=(void*)3;usb.present_=true;usb.mounted_=true;usb.generation_=10;
 uint64_t total=0,used=0;assert(usb.usage(total,used)&&total==100&&used==75&&!mutexHeld);
 uint32_t pin=0;assert(usb.pinFiles(pin)&&pin==10&&usb.validFiles(pin));
 assert(usb.requestFormat(10));usb.format(10);assert(formats==0&&usb.available()&&!usb.formatBusy()&&usb.formatState_==7);
 usb.unpinFiles();assert(usb.filePins_==0);
 assert(!usb.requestFormat(9)&&formats==0);allowed=false;assert(!usb.requestFormat(10));allowed=true;
 assert(usb.requestFormat(10));assert(!usb.requestFormat(10));usb.generation_=11;usb.format(10);
 assert(formats==0&&!usb.formatBusy());
 assert(usb.requestFormat(11));locked=true;usb.format(11);assert(formats==0&&!usb.formatBusy());locked=false;
 assert(usb.requestFormat(11));usb.format(11);assert(formats==1&&registrations==1&&usb.available()&&!usb.formatBusy()&&!mutexHeld);
 queueAccept=false;assert(!usb.requestFormat(usb.generation())&&!usb.formatBusy());
 usb.present_=false;assert(!usb.requestFormat(usb.generation())&&formats==1);
}
'''
        self.native(fake + header + '\nnamespace hp {void UsbStorageService::prepareDirectories(){}\nUsbStorageService& UsbStorageService::instance(){static UsbStorageService s;return s;}\nbool UsbStorageService::acquireFiles()' + methods + body)

    def test_settings_hold_events_usb_overview_confirmation_and_reader_leases(self):
        shell = read("src/ui/UiShell.cpp")
        for event in ("PRESSED", "PRESSING", "RELEASED"):
            self.assertIn(f"lv_indev_add_event_cb(indev, touchActivity, LV_EVENT_{event}, this)", shell)
        ui = read("src/ui/StorageUsageUi.cpp")
        for value in ("USB-Speicher", "Bestaetigung 1 von 2", "Bestaetigung 2 von 2", "usbConfirmation.confirm", "usb.requestFormat(page.usbGeneration)", "usbConfirmation.reset()"):
            self.assertIn(value, ui)
        self.assertNotIn("msc_host_vfs_format", ui)
        self.assertIn("UsbStorageService::instance().begin()", read("src/main.cpp"))
        self.assertIn("data->volumes[6]", read("src/services/StorageService.cpp"))
        self.assertEqual(read("src/services/UsbFileAccess.cpp").count("UsbFilesGuard guard"), 3)
        self.assertIn("UsbFilesGuard guard", read("src/services/SharedMediaStorage.cpp"))
        self.assertIn("format_if_mount_failed=false", read("src/services/UsbStorageService.cpp"))
