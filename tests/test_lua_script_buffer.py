from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class LuaScriptBufferTests(unittest.TestCase):
    def test_all_file_loading_paths_use_psram_buffer(self):
        for name in ('LuaScriptPreparation.h','LuaPackageInstaller.cpp'):
            text=(ROOT/'src/apps/lua'/name).read_text(encoding='utf-8')
            self.assertIn('LuaScriptBuffer script',text)
        self.assertNotIn('f.readString()',text)

    def test_real_psram_reader_full_size_fallback_failure_and_move(self):
        source=(ROOT/'src/apps/lua/LuaScriptReader.h').read_text(encoding='utf-8')
        source='\n'.join(line for line in source.splitlines() if line not in ('#pragma once','#include <Arduino.h>','#include <esp_heap_caps.h>'))
        test_system_performance.SystemPerformanceTests().native(r'''
#include <string>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cstring>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
unsigned externalAttempts=0,internalAttempts=0,live=0;
bool externalFails=false,internalFails=false;
void* heap_caps_malloc(size_t size,int caps){
 if(caps&MALLOC_CAP_SPIRAM){++externalAttempts;if(externalFails)return nullptr;}
 else{++internalAttempts;if(internalFails)return nullptr;}
 ++live;return malloc(size);
}
void heap_caps_free(void* pointer){if(pointer){--live;free(pointer);}}
struct File {std::string data;size_t at=0,limit=999999;std::atomic<bool>* cancelAfterRead=nullptr;
 size_t size()const{return data.size();}
 size_t read(uint8_t* out,size_t count){if(at>=limit)return 0;count=std::min({count,data.size()-at,limit-at});memcpy(out,data.data()+at,count);at+=count;if(cancelAfterRead)cancelAfterRead->store(true);return count;}
};
''' + source + r'''
int main(){using namespace hp;const char* error=nullptr;
 {
  File file{std::string(196608,'x')};LuaScriptBuffer script;
  assert(readLuaScript(file,script,error)&&script.length()==196608&&script.c_str()[196608]==0);
  assert(externalAttempts==1&&internalAttempts==0&&live==1);
  LuaScriptBuffer moved=std::move(script);assert(moved.length()==196608&&script.length()==0);
  script="function on_open() end";assert(script.indexOf("on_open")>=0&&live==2);
  assert(!moved.concat("x",1));
 }
 assert(live==0);
 {
  std::atomic<bool> cancelled{false};File file{std::string(196608,'x')};file.cancelAfterRead=&cancelled;LuaScriptBuffer script;
  assert(!readLuaScript(file,script,error,&cancelled)&&file.at==512&&script.length()==0&&std::string(error)=="cancelled"&&live==0);
 }
 externalFails=true;
 {
  File file{std::string(68039,'y')};LuaScriptBuffer script;
  assert(readLuaScript(file,script,error)&&internalAttempts==1);
  file.at=0;file.limit=65000;assert(!readLuaScript(file,script,error)&&script.length()==0&&std::string(error)=="lua_script_read_failed"&&live==0);
 }
 internalFails=true;
 File file{"abc"};LuaScriptBuffer script;assert(!readLuaScript(file,script,error)&&std::string(error)=="lua_memory:script_buffer"&&live==0);
}
''')
