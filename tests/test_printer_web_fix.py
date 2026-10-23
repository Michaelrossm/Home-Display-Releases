from pathlib import Path
import shutil
import subprocess
import unittest
import test_printer_service as printer_tests

ROOT=Path(__file__).resolve().parents[1]

def read(path):return (ROOT/path).read_text(encoding="utf-8")

class PrinterWebFixTests(unittest.TestCase):
    def test_normalization_ids_ports_bounds_and_actual_repeater_validation(self):
        service=read("src/services/AppWebSettingsService.cpp")
        validation=service[service.index("bool valueMatchesSimple("):service.index("bool legacyTypes(")]
        helpers=printer_tests.stripped("src/services/printer/PrinterWebProfiles.h")+"\nnamespace hp {\n"+validation+"\n}\n"
        printer_tests.PrinterServiceTests().native(helpers+r'''
int main(){using namespace hp;JsonDocument d,schema;String error,encoded;
 assert(!deserializeJson(d,R"([{"host":"192.168.1.1","protocol":"klipper","port":0},{"id":"printer_1","host":"duet.local","protocol":"rrf","port":80},{"id":"printer_1","host":"octo.local","protocol":"marlin_octoprint","port":-1}])"));
 assert(normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 assert(d[0]["id"]=="printer_2"&&d[1]["id"]=="printer_1"&&d[2]["id"]=="printer_3");
 assert(d[0]["port"]==7125&&d[2]["port"]==5000&&d[2]["protocol"]=="marlin_octoprint");
 assert(d[0]["name"]=="Drucker 1"&&d[0]["mode"]=="auto"&&d[0]["background"].as<bool>());
 assert(d[0]["calibration"]=="none"&&d[0]["alignment"]=="none");
 d[0]["calibration"]="marlin_ubl";d[0]["alignment"]="marlin_g34";assert(normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 d[0]["alignment"]="G34\nM112";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));d[0]["alignment"]="none";
 serializeJson(d,encoded);String before=encoded;assert(normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 encoded="";serializeJson(d,encoded);assert(before==encoded);
 assert(!deserializeJson(schema,R"({"key":"printer_profiles","maxItems":5,"fields":[{"key":"host","type":"host"},{"key":"protocol","type":"text"},{"key":"port","type":"integer"},{"key":"name","type":"text"}]})"));
 encoded="";assert(validateRepeaterValue(d.as<JsonVariantConst>(),schema.as<JsonObjectConst>(),encoded,error));
 d[0]["evil"]="value";encoded="";assert(!validateRepeaterValue(d.as<JsonVariantConst>(),schema.as<JsonObjectConst>(),encoded,error));d[0].remove("evil");
 schema["key"]="other_profiles";encoded="";assert(!validateRepeaterValue(d.as<JsonVariantConst>(),schema.as<JsonObjectConst>(),encoded,error));
 d[0]["port"]=1;assert(normalizePrinterWebProfiles(d.as<JsonArray>(),error)&&d[0]["port"]==1);
 d[0]["protocol"]="unsupported";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 d[0]["protocol"]="moonraker";d[0]["mode"]="wrong";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));d[0]["mode"]="auto";
 d[0]["background"]="false";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));d[0]["background"]=false;
 d[0]["enabled"]="false";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));d[0]["enabled"]=false;
 d[0]["password_secret"]="other/key";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));d[0].remove("password_secret");
 d[0]["host"]="192.168.1.1/path";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 assert(!deserializeJson(d,R"([{"host":"duet\u0000.evil","protocol":"reprap"}])"));assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 assert(!deserializeJson(d,"[1]"));assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));
 d.to<JsonArray>();for(unsigned i=0;i<6;++i)d.add<JsonObject>()["host"]="duet.local";assert(!normalizePrinterWebProfiles(d.as<JsonArray>(),error));
}''')

    def test_browser_preserves_tabs_secrets_ids_and_status_refresh_drafts(self):
        node=shutil.which("node")
        if not node:self.skipTest("Node unavailable")
        source=read("src/services/WebServerService.cpp")
        code=source[source.index("function simpleFieldInput("):source.index("async function loadNetwork(")]
        program=r'''
const assert=require('node:assert/strict');
const controls=new Map();let payload;
const $=id=>controls.get(id)||null;
for(let id of ['appSettingsStatus','appSettingsTitle','appSettingsNote','appSettingsDisplayBtn','appSettingsCard','appSettingsFields'])controls.set(id,{textContent:'',style:{},scrollIntoView(){},innerHTML:'',insertAdjacentHTML(){}});
const esc=x=>String(x??'').replaceAll('<','&lt;'),js=x=>String(x),openAppSettings=()=>{};
const f={key:'printer_profiles',type:'repeater',maxItems:5,fields:[{key:'name',type:'text'},{key:'host',type:'host'},{key:'port',type:'integer'},{key:'protocol',type:'text'}],items:[{id:'fixed_id',name:'Old',host:'duet.local',port:80,protocol:'reprap',mode:'standalone',background:false,password_secret:'key_ref'}]};
const other={key:'note',type:'text',value:'original'},secret={key:'key',type:'password',configured:true};
let appSettingsModel={id:'any_printer_app',name:'App',tabs:[{id:'profiles',fields:[f]},{id:'general',fields:[other,secret]}]},appSettingsTab='profiles';
controls.set('rep_printer_profiles_0_name',{value:'Unsaved'});controls.set('rep_printer_profiles_0_host',{value:'duet.local'});
controls.set('rep_printer_profiles_0_port',{value:'80'});controls.set('rep_printer_profiles_0_protocol',{value:'reprap'});
showAppSettingsTab('general');assert.equal(f.items[0].name,'Unsaved');
controls.delete('rep_printer_profiles_0_name');controls.delete('rep_printer_profiles_0_host');controls.delete('rep_printer_profiles_0_port');controls.delete('rep_printer_profiles_0_protocol');
controls.set('aws_note',{value:'changed'});controls.set('aws_key',{value:'secret-draft'});controls.set('aws_key_clear',{checked:true});
showAppSettingsTab('profiles');assert.equal(other.value,'changed');assert.equal(secret.draftSecret,'secret-draft');assert.equal(secret.draftClear,true);
controls.delete('aws_note');controls.delete('aws_key');controls.delete('aws_key_clear');
addRepeaterItem('printer_profiles');assert.equal(f.items[1].id,'printer_1');assert.equal(f.items[1].name,'Drucker 1');
removeRepeaterItem('printer_profiles',1);assert.equal(f.items[0].id,'fixed_id');assert.equal(f.items[0].mode,'standalone');
controls.set('rep_printer_profiles_0_name',{value:'Latest draft'});
const saved=structuredClone(appSettingsModel);saved.tabs[0].fields[0].items[0].name='Old';
saved.tabs[0].fields[0].printer_status={fixed_id:{host:'duet.local',port:80,protocol:'reprap',mode:'standalone',online:true}};
async function api(url,options){if(options){payload=JSON.parse(options.body);return {changed:2}}return saved}
(async()=>{
 await refreshPrinterStatus();assert.equal(f.items[0].name,'Latest draft');assert.equal(f.printer_online,1);
 assert.equal(printerStateFor(f,{...f.items[0],host:'different.local'}),null);
 assert.ok(repeaterItemHtml(f,f.items[0],0).includes('Verbunden'));
 const unknown=repeaterItemHtml(f,{id:'new',name:'<script>'},1);assert.ok(unknown.includes('&lt;script>'));assert.ok(unknown.includes('Noch keine Statusdaten'));
 await saveAppWebSettings();assert.equal(payload.id,'any_printer_app');
 assert.equal(payload.values.printer_profiles[0].id,'fixed_id');assert.equal(payload.values.printer_profiles[0].name,'Latest draft');
 assert.equal(payload.values.printer_profiles[0].password_secret,'key_ref');assert.equal(payload.values.printer_profiles[0].background,false);
 assert.equal(payload.values.note,'changed');assert.equal(payload.values.key,'secret-draft');assert.deepEqual(payload.clearSecrets,['key']);
})().catch(e=>{console.error(e);process.exit(1)});
'''
        result=subprocess.run([node,"-e",code+program],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_generic_scoped_cache_and_prevalidation_before_writes(self):
        settings=read("src/services/AppWebSettingsService.cpp")
        apply=settings.split("bool AppWebSettingsService::apply",1)[1]
        self.assertLess(apply.index("normalizePrinterWebProfiles"),apply.index("size_t changed = 0"))
        self.assertIn('executeLuaStorage(appId, 1, key, wrapped)',apply)
        self.assertNotIn('appId == "printer_panel"',settings)
        cached=read("src/services/PrinterService.cpp").split("String PrinterService::cachedSnapshot",1)[1].split("String PrinterService::files",1)[0]
        self.assertIn("identity(e->profile)==identity(expected)",cached)
        for forbidden in ("executeHttp",".sync=","lastSeen=","arm(","submit("):
            self.assertNotIn(forbidden,cached)
