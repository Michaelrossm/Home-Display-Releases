import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding="utf-8")

class LuaPlatformTests(unittest.TestCase):
    def test_real_runtime_json_and_energy_model(self):
        lua=ROOT/"managed_components/espressif__lua/lua"
        json_dir=ROOT/".pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src"
        if not (lua/"lua.h").exists() or not (json_dir/"ArduinoJson.h").exists():
            self.skipTest("Pinned dependencies required")
        runtime=source("src/apps/lua/LuaApp.cpp")
        functions=runtime[runtime.index("constexpr const char* kRegistryKey"):runtime.index("void registerBridge")]
        with tempfile.TemporaryDirectory(prefix="lua-platform-") as temp:
            folder=Path(temp)
            (folder/"runtime.inc").write_text(functions,encoding="utf-8")
            storage=source("src/apps/lua/LuaStorageBridge.cpp")
            names=storage[storage.index("bool nameValid"):storage.index("String stored")]
            paths=storage[storage.index("bool fileNameValid"):storage.index("bool usage")]
            worker=source("src/apps/lua/LuaJobService.cpp")
            addresses=worker[worker.index("bool localAddress"):worker.index("// Resolve on worker")]
            (folder/"validators.inc").write_text(names+paths+addresses,encoding="utf-8")
            jobs=worker[worker.index("LuaJobService& LuaJobService::instance"):worker.index("void LuaJobService::execute")]
            (folder/"jobs.inc").write_text(jobs,encoding="utf-8")
            platform=source("src/apps/lua/LuaPlatformBridge.cpp")
            (folder/"platform.inc").write_text('\n'.join(line for line in platform.splitlines() if not line.startswith('#include')),encoding="utf-8")
            (folder/"Arduino.h").write_text(r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
class String:public std::string {
public:
 using std::string::string;
 using std::string::operator=;
 bool startsWith(const char* p)const{return rfind(p,0)==0;}
 bool endsWith(const char* p)const{const size_t n=strlen(p);return size()>=n&&compare(size()-n,n,p)==0;}
 int indexOf(const char* p)const{const auto i=find(p);return i==npos?-1:static_cast<int>(i);}
 int indexOf(char p)const{const auto i=find(p);return i==npos?-1:static_cast<int>(i);}
};
inline size_t strlcpy(char* out,const char* input,size_t size){const size_t n=strlen(input);if(size){memcpy(out,input,n<size-1?n:size-1);out[n<size-1?n:size-1]=0;}return n;}
uint32_t millis();
''',encoding="utf-8")
            (folder/"freertos").mkdir()
            (folder/"freertos/FreeRTOS.h").write_text('#pragma once\n#include <cstdint>\nusing TickType_t=uint32_t;\nconstexpr int pdTRUE=1;\n',encoding="utf-8")
            (folder/"freertos/semphr.h").write_text(r'''
#pragma once
#include <cassert>
struct TestSemaphore {bool held=false;};
using SemaphoreHandle_t=TestSemaphore*;
extern bool forceBusyLock;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static TestSemaphore mutex;return &mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t mutex,uint32_t wait){assert(wait==0);if(forceBusyLock||mutex->held)return 0;mutex->held=true;return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t mutex){assert(mutex->held);mutex->held=false;}
''',encoding="utf-8")
            (folder/"esp_heap_caps.h").write_text(r'''
#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern size_t hp_test_livebytes;
struct alignas(std::max_align_t) TestAllocation {size_t bytes;};
inline void* heap_caps_malloc(size_t n,int){auto* p=static_cast<TestAllocation*>(malloc(sizeof(TestAllocation)+n));if(!p)return nullptr;p->bytes=n;hp_test_livebytes+=n;return p+1;}
inline void heap_caps_free(void* p){if(!p)return;auto* h=static_cast<TestAllocation*>(p)-1;hp_test_livebytes-=h->bytes;free(h);}
inline void* heap_caps_realloc(void* p,size_t n,int caps){if(!p)return heap_caps_malloc(n,caps);auto* h=static_cast<TestAllocation*>(p)-1;auto* next=heap_caps_malloc(n,caps);if(!next)return nullptr;memcpy(next,p,h->bytes<n?h->bytes:n);heap_caps_free(p);return next;}
''',encoding="utf-8")
            c_sources=[str(p) for p in lua.glob("*.c") if p.name not in ("lua.c","luac.c","onelua.c")]
            fixture=ROOT/"tests/native/lua_platform.cpp"
            bridge=ROOT/"src/apps/lua/LuaJsonBridge.cpp"
            compiler=shutil.which("g++") or shutil.which("clang++")
            if compiler:
                cc=shutil.which("gcc") or shutil.which("clang")
                if not cc:self.skipTest("Host C compiler unavailable")
                cmd=[cc,"-O1","-DLUA_32BITS=1","-c",*c_sources]
                result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                cmd=[compiler,"-std=c++17","-DLUA_32BITS=1",str(fixture),str(bridge),"-I",str(folder),"-I",str(ROOT),"-I",str(lua),"-I",str(json_dir),*[str(p) for p in folder.glob("*.o")],"-o",str(folder/"check")]
                if os.name!="nt":cmd += ["-lm","-ldl"]
                exe=folder/"check"
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists():self.skipTest("Host compiler unavailable")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                if not install:self.skipTest("Host compiler unavailable")
                vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                quoted=" ".join(f'"{s}"' for s in c_sources)
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /O1 /DLUA_32BITS=1 /c {quoted}"'
                result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout[-3000:]+result.stderr)
                includes=" ".join(f'/I"{p}"' for p in (folder,ROOT,lua,json_dir))
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /DLUA_32BITS=1 {includes} "{fixture}" "{bridge}" *.obj /Fe:check.exe"'
                exe=folder/"check.exe"
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout[-5000:]+result.stderr)
            result=subprocess.run([str(exe),str(ROOT)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            # A firmware-only checkout has no external app packages. Core
            # sandbox/job/JSON checks must still execute and succeed.
            result=subprocess.run([str(exe),str(folder)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_queue_and_owned_results(self):
        network=source("src/apps/lua/LuaNetworkBridge.cpp")
        self.assertNotRegex(network,r"esp_http_client_|getaddrinfo|portMAX_DELAY|delay\(")
        worker=source("src/apps/lua/LuaJobService.cpp")
        submit=worker.split("uint32_t LuaJobService::submit",1)[1].split("bool LuaJobService::result",1)[0]
        self.assertNotIn("portMAX_DELAY",submit)
        self.assertIn('NetworkWorker::instance().submit',submit)
        self.assertIn('slot.request.owner==owner',worker)
        self.assertIn('now-slot.finished)>60000',worker)
        self.assertIn('disable_auto_redirect=true',worker)
        self.assertIn('NetworkInternet',worker)
        self.assertNotIn('xTaskCreate',worker)

    def test_storage_namespace_and_limits(self):
        storage=source("src/apps/lua/LuaStorageBridge.cpp")
        self.assertIn('doc["owner"].as<String>()==owner',storage)
        self.assertIn('doc["name"].as<String>()==name',storage)
        self.assertIn('storage_collision',storage)
        self.assertIn('name.indexOf("..")',storage)
        self.assertIn('name.startsWith("/")',storage)
        self.assertIn('file_quota',storage)
        self.assertNotIn('"hp_secret_get"',storage)
        self.assertIn('writeBinaryAtomic',storage)

    def test_ui_event_ownership_and_negative_chart(self):
        runtime=source("src/apps/lua/LuaApp.cpp")
        events=source("src/apps/lua/LuaEvents.cpp")
        self.assertIn('xQueueSend(queue(),&event,0)',events)
        self.assertNotIn('lua_',events)
        self.assertIn('protectedCall(L,2,0',runtime)
        self.assertIn('bindings_.size()>=256', ''.join(runtime.split()))
        ui=source("src/ui/UiShell.cpp").split("void UiShell::loop",1)[1]
        self.assertLess(ui.index('DisplayUiGuard'),ui.index('LuaApp::dispatchEvents'))
        chart=source("src/apps/lua/LuaChartBridge.cpp")
        self.assertNotIn('value <= 0',chart)
        self.assertIn('value<-1000000000',chart)
        self.assertIn('state->series>=4',chart)
        self.assertIn('lv_obj_set_style_bg_color(chart,theme.card()',chart)
        self.assertIn('lv_obj_set_style_line_color(chart,theme.border()',chart)
        self.assertIn('current[i]!=values[i]',chart)

    def test_enable_and_return_from_settings(self):
        manager=source("src/core/AppManager.cpp")
        self.assertIn('current_ == app && runtimeStatus(id).state == AppRuntimeState::Open',manager)
        self.assertIn('app->userInstallable() && !isEnabled(app)',manager)
        settings=source("src/apps/settings/DynamicAppSettings.cpp")
        self.assertIn('!app->userInstallable()) continue',settings)
        self.assertIn('lv_switch_create(row)',settings)
        self.assertIn('instance().invalidate()',settings)

if __name__=="__main__":unittest.main()
