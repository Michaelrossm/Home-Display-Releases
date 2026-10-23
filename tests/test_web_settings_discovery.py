import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WebSettingsDiscoveryTests(unittest.TestCase):
    def test_native_schema_validation(self):
        shim = r'''
#pragma once
#include <string>
#include <cstdint>
#include <cctype>
#include <algorithm>
#include <type_traits>
class String:public std::string {public:using std::string::string;using std::string::operator=;
String(const std::string& s):std::string(s){}bool startsWith(const char* s)const{return find(s)==0;}
template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>String(T value):std::string(std::to_string(value)){}
size_t write(uint8_t c){push_back(c);return 1;}size_t write(const uint8_t* data,size_t n){append(reinterpret_cast<const char*>(data),n);return n;}
void toLowerCase(){std::transform(begin(),end(),begin(),[](unsigned char c){return std::tolower(c);});}};
#include <ArduinoJson.h>
inline void convertFromJson(JsonVariantConst v,String& s){s=v.as<std::string>();}
'''
        program = r'''
#include <cassert>
#include <mutex>
#include <vector>
#include "WebSettingsSchemaValidator.h"
using namespace hp;
uint32_t clockMs=1000;uint32_t millis(){return clockMs;}
struct NetworkService{bool connected=true;static NetworkService& instance(){static NetworkService n;return n;}bool online(){return connected;}};
struct MdnsDiscoveryResult{String service,hostname,host;uint16_t port;String product;};
std::vector<MdnsDiscoveryResult> candidates;int searches=0;
int queryMdnsServices(const String* services,size_t count,MdnsDiscoveryResult* out,size_t capacity,uint32_t timeout,int* error){
assert(count==3&&capacity==8&&timeout==1500);assert(services[0]=="_moonraker._tcp.local");++searches;*error=0;
for(size_t i=0;i<candidates.size();++i)out[i]=candidates[i];return candidates.size();}
#include "worker.inc"
int main(){JsonDocument doc;String error;
assert(!deserializeJson(doc,R"({"schema":1,"tabs":[{"id":"devices","label":"Devices","fields":[{"key":"profiles","label":"Profiles","type":"repeater","fields":[{"key":"ip","label":"IP","type":"host"},{"key":"port","label":"Port","type":"integer"},{"key":"name","label":"Name","type":"text"},{"key":"type","label":"Protocol","type":"select","options":["klipper","rrf","octoprint"]}],"discovery":{"type":"printers","map":{"host":"ip","port":"port","hostname":"name","protocol":"type"},"protocolValues":{"moonraker":"klipper"}}}]}]})"));
assert(WebSettingsSchemaValidator::validate(doc,error));
auto field=doc["tabs"][0]["fields"][0];
field["discovery"]["map"]["port"]="ip";assert(!WebSettingsSchemaValidator::validate(doc,error));
field["discovery"]["map"]["port"]="port";
field["discovery"]["map"]["host"]="missing";assert(!WebSettingsSchemaValidator::validate(doc,error));
field["discovery"]["map"]["host"]="ip";
field["discovery"]["type"]="arbitrary_subnet_scan";assert(!WebSettingsSchemaValidator::validate(doc,error));
field["discovery"]["type"]="printers";
field["discovery"]["protocolValues"]["unknown"]="klipper";assert(!WebSettingsSchemaValidator::validate(doc,error));
field.remove("discovery");assert(WebSettingsSchemaValidator::validate(doc,error));
webDiscovery.busy=true;webDiscovery.queued=clockMs;
candidates={{"_http._tcp.local","printer.local","192.168.1.10",7125,""},
{"_moonraker._tcp.local","printer.local","192.168.1.10",7125,""},
{"_http._tcp.local","duet.local","192.168.1.20",80,"RepRapFirmware"},
{"_octoprint._tcp.local","octopi.local","192.168.1.30",5000,""},
{"_http._tcp.local","octoprint-named-but-unknown.local","192.168.1.40",80,""}};
runWebDiscovery(nullptr);assert(!webDiscovery.busy);assert(!deserializeJson(doc,webDiscovery.results.c_str()));
assert(doc.size()==4&&doc[0]["protocol"]=="moonraker"&&doc[1]["protocol"]=="rrf"&&doc[2]["protocol"]=="octoprint"&&doc[3]["protocol"]=="unknown");
NetworkService::instance().connected=false;runWebDiscovery(nullptr);assert(searches==1);
assert(webDiscovery.message.find("offline")!=std::string::npos);
clockMs+=16000;runWebDiscovery(nullptr);assert(searches==1);
assert(webDiscovery.message.find("abgelaufen")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory(prefix="web-discovery-") as tmp:
            folder = Path(tmp)
            (folder / "Arduino.h").write_text(shim, encoding="utf-8")
            (folder / "check.cpp").write_text(program, encoding="utf-8")
            service = (ROOT / "src/services/AppWebSettingsService.cpp").read_text(encoding="utf-8")
            worker = service[service.index("struct WebDiscoveryRun"):service.index("} // namespace", service.index("struct WebDiscoveryRun"))]
            (folder / "worker.inc").write_text(worker, encoding="utf-8")
            json_dir = ROOT / ".pio/libdeps/home_display_p4_r3_dev/ArduinoJson/src"
            source = ROOT / "src/services/WebSettingsSchemaValidator.cpp"
            headers = ROOT / "src/services"
            exe = folder / ("check.exe" if os.name == "nt" else "check")
            compiler = shutil.which("g++") or shutil.which("clang++")
            if compiler:
                cmd = [compiler, "-std=c++17", "-I", str(folder), "-I", str(json_dir), "-I", str(headers), str(source), str(folder / "check.cpp"), "-o", str(exe)]
            else:
                setup = Path("C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
                if not setup.exists(): self.skipTest("Host compiler unavailable")
                cmd = f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 /utf-8 /I"{folder}" /I"{json_dir}" /I"{headers}" "{source}" check.cpp /Fe:check.exe"'
            result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_browser_selection_is_explicit_and_preserves_manual_profiles(self):
        node = shutil.which("node")
        if not node: self.skipTest("Node unavailable")
        source = (ROOT / "src/services/WebServerService.cpp").read_text(encoding="utf-8")
        code = source[source.index("function validateDiscoverySchema("):source.index("function repeaterItemHtml(")]
        code = next(line for line in source.splitlines() if line.startswith("function validateWebSettingsBytes(")) + "\n" + code
        program = r'''
const assert=require('node:assert/strict');
let alerts=[],rendered=0,appSettingsModel={id:'example'},status={textContent:''};
const $=id=>id==='appSettingsStatus'?status:{querySelectorAll:()=>[]};
let f={key:'profiles',type:'repeater',maxItems:3,fields:[{key:'ip',type:'host'},{key:'port',type:'integer'},{key:'type',type:'select',options:[{value:'klipper',label:'Klipper'}]}],items:[{ip:'192.168.1.5',port:80}],discovery:{type:'printers',map:{host:'ip',port:'port',protocol:'type'},protocolValues:{moonraker:'klipper'}}};
appSettingsModel.tabs=[{fields:[f]}];
const findRepeater=()=>f,renderAppSettings=()=>rendered++,alert=m=>alerts.push(m),readSimpleValue=()=>undefined;
validateDiscoverySchema(f);assert.throws(()=>validateDiscoverySchema({...f,discovery:{type:'printers',map:{host:'missing'}}}));
f.label='Profiles';f.fields.forEach(c=>c.label=c.key);
const schema={schema:1,tabs:[{id:'printers',label:'Printers',fields:[f]}]};
validateWebSettingsBytes(new TextEncoder().encode(JSON.stringify(schema)));
const invalid=JSON.parse(JSON.stringify(schema));invalid.tabs[0].fields[0].discovery.map.host='missing';
assert.throws(()=>validateWebSettingsBytes(new TextEncoder().encode(JSON.stringify(invalid))));
appDiscoveryResults.set('profiles',{model:appSettingsModel,devices:[{host:'192.168.1.10',port:7125,protocol:'moonraker'},{host:'192.168.1.20',port:80,protocol:'unknown'}]});
useAppDiscovery('profiles',1);assert.equal(f.items.length,1);
useAppDiscovery('profiles',0);assert.equal(f.items.length,2);assert.equal(f.items[1].type,'klipper');assert.equal(f.items[0].ip,'192.168.1.5');
useAppDiscovery('profiles',0);assert.equal(f.items.length,2);assert.equal(rendered,1);
assert.match(status.textContent,/noch nicht gespeichert/);
appDiscoveryResults.get('profiles').model={id:'other'};useAppDiscovery('profiles',0);assert.equal(f.items.length,2);
'''
        result = subprocess.run([node, "-e", code + program], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_security_bounds_and_existing_worker(self):
        service = (ROOT / "src/services/AppWebSettingsService.cpp").read_text(encoding="utf-8")
        web = (ROOT / "src/services/WebServerService.cpp").read_text(encoding="utf-8")
        route = web.split("esp_err_t WebServerService::appWebSettingsDiscoveryHandler", 1)[1].split("esp_err_t ", 1)[0]
        self.assertIn("writeAuthorized(req)", route)
        self.assertIn("req->content_len>512", route)
        discovery = service.split("bool AppWebSettingsService::discover", 1)[1].split("AppWebSettingsService&", 1)[0]
        for guard in ("AppPermission::Discovery", "AppPermission::Network", "validateSchema", "webDiscovery.owner!=appId", "webDiscovery.field!=field", "webDiscovery.busy", "30000", "5000", "NetworkWorker::instance().submit"):
            self.assertIn(guard, discovery)
        worker = service.split("void runWebDiscovery", 1)[1].split("} // namespace", 1)[0]
        self.assertIn("found[8]", worker)
        self.assertIn("1500,&queryError", worker)
        self.assertIn("15000", worker)
        for forbidden in ("xTaskCreate", "HTTPClient", "writeRecord", "putString", "lv_", "app->"):
            self.assertNotIn(forbidden, worker)
        self.assertIn("Drucker können trotzdem vorhanden sein", worker)
        self.assertIn("textContent=device.host", web)


if __name__ == "__main__": unittest.main()
