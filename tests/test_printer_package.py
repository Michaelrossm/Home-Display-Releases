import importlib.util
from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("lua_syntax", ROOT / "tools/lua_syntax.py")
syntax = importlib.util.module_from_spec(spec)
spec.loader.exec_module(syntax)

class PrinterPackageTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / "managed_components/espressif__lua/lua/lua.h").is_file(), "Pinned Lua parser requires downloaded build dependencies")
    def test_pinned_parser_and_truncation_regression(self):
        with syntax.LuaSyntaxChecker() as checker:
            for path in (ROOT / "apps/printer_panel_professional").glob("*.lua"):
                checker.check(path.read_bytes(), path.name)
            with self.assertRaisesRegex(ValueError, "expected|unfinished"):
                checker.check(b'function on_open() print("truncated"', "main.lua")
            checker.check(b'error("must not execute"); function on_open() end', "safe.lua")

    def test_reader_checks_complete_content_before_parsing(self):
        reader = (ROOT / "src/apps/lua/LuaScriptReader.h").read_text(encoding="utf-8")
        self.assertIn("script.length() != expected", reader)
        self.assertIn("script.reserve(expected)", reader)
        self.assertIn("lua_script_read_failed", reader)
        for name in ("LuaPackageInstaller.cpp", "LuaScriptPreparation.cpp"):
            source = (ROOT / "src/apps/lua" / name).read_text(encoding="utf-8")
            self.assertIn("readLuaScript(", source)
            self.assertNotIn("f.readString()", source)

    @unittest.skipUnless((ROOT / "apps/printer_panel_professional/main.lua").is_file(), "Optional local printer package not present")
    def test_live_refresh_is_not_starved_by_preview_polling(self):
        script = (ROOT / "apps/printer_panel_professional/main.lua").read_text(encoding="utf-8").split("function on_tick()", 1)[1]
        self.assertLess(script.index("refreshAt=now;refresh();return"), script.index("if previewJob and"))
        self.assertLess(script.index("renderPagePart(tickPhase)"), script.index("if previewJob and"))
        self.assertIn("if commandBusy and now-diagnosticAt>=300", script)

    def test_actual_reader_short_read_memory_binary_and_chunking(self):
        reader = (ROOT / "src/apps/lua/LuaScriptReader.h").read_text(encoding="utf-8")
        reader = "\n".join(line for line in reader.splitlines() if line not in ("#pragma once", "#include <Arduino.h>", "#include <esp_heap_caps.h>"))
        test_system_performance.SystemPerformanceTests().native(r'''
#include <string>
#include <cassert>
#include <cstdint>
bool allocationFailure=false,appendFailure=false;
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t size,int){return malloc(size);}
void heap_caps_free(void* pointer){free(pointer);}
struct String:std::string {
 using std::string::operator=;
 bool reserve(size_t count){if(allocationFailure)return false;std::string::reserve(count);return true;}
 bool concat(const char* data,unsigned count){if(appendFailure)return false;append(data,count);return true;}
 size_t length()const{return size();}
};
struct TestFile {
 std::string data;size_t offset=0,cutoff=999999,reported=0;
 size_t size()const{return reported?reported:data.size();}
 size_t read(uint8_t* out,size_t count){
  if(offset>=cutoff||offset>=data.size())return 0;
  count=std::min({count,data.size()-offset,cutoff-offset,size_t(73)});
  memcpy(out,data.data()+offset,count);offset+=count;return count;
 }
};
''' .replace('#include <cstdint>', '#include <cstdint>\n#include <algorithm>\n#include <cstring>') + reader + r'''
int main(){String script;const char* error=nullptr;
 TestFile complete{std::string(57754,'x')};assert(hp::readLuaScript(complete,script,error)&&script.size()==57754&&!error);
 TestFile shortRead{complete.data};shortRead.cutoff=53000;assert(!hp::readLuaScript(shortRead,script,error)&&script.empty()&&std::string(error)=="lua_script_read_failed");
 TestFile missingTail{"abc"};missingTail.reported=10;assert(!hp::readLuaScript(missingTail,script,error)&&script.empty());
 TestFile binary{std::string("a\0b",3)};assert(!hp::readLuaScript(binary,script,error)&&std::string(error)=="lua_script_binary");
 allocationFailure=true;TestFile memory{"abc"};assert(!hp::readLuaScript(memory,script,error)&&std::string(error)=="lua_memory:script_buffer");allocationFailure=false;
 appendFailure=true;assert(!hp::readLuaScript(memory,script,error)&&script.empty());appendFailure=false;
 TestFile huge{std::string(196609,'x')};assert(!hp::readLuaScript(huge,script,error)&&std::string(error)=="lua_script_size");
}
''')
