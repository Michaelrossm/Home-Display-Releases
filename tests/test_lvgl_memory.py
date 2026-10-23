"""Exercise the production PSRAM policy with LVGL's pinned TLSF allocator."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class LvglMemoryTests(unittest.TestCase):
    def test_retained_views_and_render_scratch(self):
        vendor = ROOT / "managed_components/lvgl__lvgl/src/stdlib/builtin"
        if not (vendor / "lv_tlsf.c").exists():
            self.skipTest("Pinned LVGL dependency required")
        # Keep the allocator implementation intact; replace only its platform
        # includes/log/assert glue for this native executable.
        strip = lambda text: re.sub(r'^#include .*$', '', text, flags=re.M)
        header = strip((vendor / "lv_tlsf.h").read_text())
        allocator = strip((vendor / "lv_tlsf.c").read_text())
        with tempfile.TemporaryDirectory(prefix="hp-lvgl-memory-") as directory:
            folder = Path(directory)
            (folder / "esp_heap_caps.h").write_text('''#pragma once
#include <cstdlib>
enum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2};
unsigned allocationCaps=0,allocationCalls=0;
inline void* heap_caps_malloc(size_t bytes,unsigned caps){allocationCaps=caps;++allocationCalls;return malloc(bytes);}
''')
            (folder / "esp_log.h").write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n')
            shutil.copyfile(ROOT / "include/LvglMemoryConfig.h", folder / "LvglMemoryConfig.h")
            program = '''#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "LvglMemoryConfig.h"
#define LV_USE_STDLIB_MALLOC 1
#define LV_STDLIB_BUILTIN 1
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_USE_OS 0
#define LV_LOG_ERROR(...) ((void)0)
#define LV_ASSERT(x) assert(x)
#define LV_UNUSED(x) ((void)(x))
#define lv_memcpy memcpy
'''+header+allocator+'''
int main(){
 static_assert(LV_MEM_SIZE==4U*1024U*1024U,"Bounded PSRAM pool");
 void* memory=LV_MEM_POOL_ALLOC(LV_MEM_SIZE);assert(memory);
 assert(allocationCalls==1&&allocationCaps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 auto pool=lv_tlsf_create_with_pool(memory,LV_MEM_SIZE);assert(pool);
 std::vector<void*> views[4];
 for(unsigned round=0;round<300;++round){auto& view=views[round%4];
   for(auto ptr:view)lv_tlsf_free(pool,ptr);view.clear();
   for(unsigned i=0;i<256;++i){auto ptr=lv_tlsf_malloc(pool,256+(i%7)*64);assert(ptr);view.push_back(ptr);}
   void* shadow=lv_tlsf_malloc(pool,1352);assert(shadow);
   void* frame=lv_tlsf_malloc(pool,512*1024);assert(frame);
   lv_tlsf_free(pool,frame);lv_tlsf_free(pool,shadow);assert(lv_tlsf_check(pool)==0);
 }
 for(auto& view:views)for(auto ptr:view)lv_tlsf_free(pool,ptr);
 assert(lv_tlsf_check(pool)==0);free(memory);
 // The old 64-KiB pool cannot retain even one equivalent view.
 memory=malloc(64*1024);pool=lv_tlsf_create_with_pool(memory,64*1024);
 unsigned count=0;while(count<256&&lv_tlsf_malloc(pool,512))++count;
 assert(count<256);free(memory);
}
'''
            (folder / "check.cpp").write_text(program)
            exe = folder / ("check.exe" if os.name == "nt" else "check")
            compiler = shutil.which("g++") or shutil.which("clang++")
            if compiler:
                cmd = [compiler,"-std=c++17","-I",str(folder),"check.cpp","-o",str(exe)]
            else:
                vswhere = Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists(): self.skipTest("Host C++ compiler unavailable")
                install = subprocess.check_output([str(vswhere),"-latest","-products","*","-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                if not install: self.skipTest("Host C++ compiler unavailable")
                vcvars = Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" check.cpp /Fe:check.exe"'
            result = subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result = subprocess.run([str(exe)],cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_policy_reaches_managed_lvgl_and_does_not_spin_on_oom(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('target_compile_options(__idf_lvgl__lvgl PRIVATE',cmake)
        self.assertIn('-include${CMAKE_SOURCE_DIR}/include/LvglMemoryConfig.h',cmake)
        policy = (ROOT / "include/LvglMemoryConfig.h").read_text()
        self.assertIn('#define LV_ASSERT_HANDLER abort();',policy)
        self.assertNotIn('MALLOC_CAP_INTERNAL',policy)
