"""Exercise the production native service with simulated clock/NVS/crypto hardware.
The deterministic crypto double tests verifier use, not SHA-256 implementation.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class SecurityTests(unittest.TestCase):
    def test_native_pin_lifecycle_and_cooldowns(self):
        with tempfile.TemporaryDirectory(prefix="hp-security-") as name:
            folder=Path(name)
            for f in ("SecurityService.cpp","SecurityService.h"):
                shutil.copyfile(ROOT/"src/services"/f,folder/f)
            for d in ("freertos","mbedtls"): (folder/d).mkdir()
            (folder/"Arduino.h").write_text(r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
class String:public std::string {
public:using std::string::string;using std::string::operator=;
String(const std::string& s):std::string(s){}
size_t length()const{return size();}
String substring(size_t a,size_t b=std::string::npos)const{return substr(a,b==std::string::npos?b:b-a);}
};
extern uint32_t testClock;
inline uint32_t millis(){return testClock;}
''',encoding="utf-8")
            (folder/"freertos/FreeRTOS.h").write_text('#pragma once\n#include <cstdint>\nconstexpr uint32_t portMAX_DELAY=0xffffffff;\n')
            (folder/"freertos/semphr.h").write_text('#pragma once\nusing SemaphoreHandle_t=void*;\ninline void* xSemaphoreCreateRecursiveMutex(){return reinterpret_cast<void*>(1);}\ninline void xSemaphoreTakeRecursive(void*,uint32_t){}\ninline void xSemaphoreGiveRecursive(void*){}\n')
            (folder/"esp_random.h").write_text('#pragma once\ninline void esp_fill_random(void* out,size_t n){static unsigned char seed=1;memset(out,seed++,n);}\n')
            (folder/"mbedtls/sha256.h").write_text(r'''
#pragma once
inline int mbedtls_sha256(const uint8_t* p,size_t n,uint8_t* out,int){
uint32_t hash=2166136261u;for(size_t i=0;i<n;++i)hash=(hash^p[i])*16777619u;
for(unsigned i=0;i<32;++i){hash=hash*1664525u+1013904223u;out[i]=hash>>24;}return 0;}
''')
            (folder/"ConfigService.h").write_text(r'''
#pragma once
#include "Arduino.h"
#include <map>
namespace hp {class ConfigService {
public:std::map<std::string,String> secrets;std::map<std::string,uint32_t> values;bool writes=true;
static ConfigService& instance(){static ConfigService c;return c;}
bool writable(){return writes;}bool credentialsReady(){return true;}bool hasSecret(const char* k){return secrets.count(k)&&secrets[k].length();}String getSecretString(const char* k){return secrets[k];}
uint32_t getU32(const char* k){return values[k];}bool getBool(const char* k,bool fallback){return values.count(k)?values[k]!=0:fallback;}
bool putU32(const char* k,uint32_t v){if(!writes)return false;values[k]=v;return true;}
bool putBool(const char* k,bool v){return putU32(k,v);}
bool putSecretString(const char* k,const String& v){if(!writes)return false;secrets[k]=v;return true;}
bool removeSecret(const char* k){if(!writes)return false;secrets.erase(k);return true;}
};}
''')
            (folder/"check.cpp").write_text(r'''
#include "SecurityService.h"
#include "ConfigService.h"
#include <cassert>
uint32_t testClock=1000;
int main(){using namespace hp;auto& s=SecurityService::instance();auto& c=ConfigService::instance();
assert(s.begin());assert(!s.enabled(PinKind::Settings));assert(!s.tabletLocked());assert(s.settingsAllowed());
for(auto pin:{"","1","123","12ab","1234567890123"})assert(!s.setPin(PinKind::Settings,"",pin,pin));
assert(!s.setPin(PinKind::Settings,"","1234","5678"));
assert(s.setPin(PinKind::Settings,"","1234","1234"));assert(!s.settingsAllowed());
assert(c.secrets["sec_set_pin"].length()==97);assert(c.secrets["sec_set_pin"]!="1234");
assert(s.setPin(PinKind::Tablet,"","5678","5678"));assert(!s.tabletLocked());
assert(!s.unlock(PinKind::Settings,"5678"));assert(s.unlock(PinKind::Settings,"1234"));assert(s.settingsAllowed());
s.lockSettings();assert(!s.settingsAllowed());s.lockTablet();assert(s.tabletLocked());
assert(!s.unlock(PinKind::Tablet,"1234"));assert(s.unlock(PinKind::Tablet,"5678"));assert(!s.tabletLocked());
for(auto kind:{PinKind::Settings,PinKind::Tablet}){
assert(s.unlock(kind,kind==PinKind::Settings?"1234":"5678"));
for(unsigned attempt=1;attempt<=20;++attempt){assert(!s.unlock(kind,"0000"));
unsigned wait=attempt==5?120:attempt==10?300:attempt==15?600:attempt==20?1800:0;
assert(s.blockedSeconds(kind)==wait);if(wait){assert(!s.unlock(kind,kind==PinKind::Settings?"1234":"5678"));
assert(s.blockedSeconds(kind)==wait);testClock+=wait*1000-1;assert(s.blockedSeconds(kind)==1);++testClock;assert(s.blockedSeconds(kind)==0);}}
assert(!s.removePin(kind,"0000"));assert(s.blockedSeconds(kind)==1800);testClock+=1800000;
assert(s.unlock(kind,kind==PinKind::Settings?"1234":"5678"));assert(s.blockedSeconds(kind)==0);}
assert(s.setPin(PinKind::Settings,"1234","4321","4321"));assert(s.unlock(PinKind::Tablet,"5678"));
assert(!s.unlock(PinKind::Settings,"1234"));assert(s.unlock(PinKind::Settings,"4321"));
assert(!s.removePin(PinKind::Settings,""));assert(s.removePin(PinKind::Settings,"4321"));assert(!s.enabled(PinKind::Settings));
assert(s.enabled(PinKind::Tablet));assert(s.removePin(PinKind::Tablet,"5678"));assert(!s.tabletLocked());
// Reconstruct a separate service over persisted NVS to simulate reboot.
assert(s.setPin(PinKind::Tablet,"","9999","9999"));s.lockTablet();
for(unsigned i=0;i<5;++i)assert(!s.unlock(PinKind::Tablet,"0000"));
SecurityService reboot;assert(reboot.begin());assert(reboot.tabletLocked());assert(reboot.blockedSeconds(PinKind::Tablet)==120);
testClock+=120000;assert(reboot.unlock(PinKind::Tablet,"9999"));
c.writes=false;assert(!reboot.setPin(PinKind::Tablet,"9999","8888","8888"));assert(!reboot.unlock(PinKind::Tablet,"9999"));
assert(reboot.tabletLocked()&&reboot.blockedSeconds(PinKind::Tablet)==1800);
c.writes=true;testClock+=1800000;assert(reboot.blockedSeconds(PinKind::Tablet)==0);testClock+=0x80000000u;assert(reboot.blockedSeconds(PinKind::Tablet)==0);
c.secrets["sec_tab_pin"]=String(97,'z');SecurityService malformed;assert(!malformed.begin()&&malformed.tabletLocked());
return 0;}
''')
            compiler=shutil.which("g++") or shutil.which("clang++")
            exe=folder/("check.exe" if os.name=="nt" else "check")
            if compiler: cmd=[compiler,"-std=c++17","-I",str(folder),"SecurityService.cpp","check.cpp","-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists():self.skipTest("Host compiler missing")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" SecurityService.cpp check.cpp /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_package_and_native_enforcement(self):
        settings=(ROOT/"src/apps/radio/RadioSettings.cpp").read_text()
        self.assertIn("SecurityWriteGuard guard",settings)
        self.assertIn("!SecurityService::instance().settingsAllowed()",settings)
        for field in ("parentalEnabled","volumeLimitEnabled","maxVolumePercent","lockSources","lockPowerOff","lockDisplayControls"):
            self.assertIn(f"settings.{field}=old.{field}",settings)
        ui=(ROOT/"src/ui/SecurityUi.cpp").read_text()
        self.assertIn("lv_layer_sys()",ui);self.assertIn("LV_OPA_COVER",ui)
        bridge=(ROOT/"src/apps/lua/LuaSecurityBridge.cpp").read_text()
        self.assertNotIn("getSecret",bridge);self.assertNotIn("unlockTablet",bridge)
