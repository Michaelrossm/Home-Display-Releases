from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding="utf-8")
def stripped(path): return "\n".join(x for x in source(path).splitlines() if not x.startswith("#include") and x != "#pragma once")

class PrinterServiceTests(unittest.TestCase):
    def test_console_read_independent_of_status_and_empty_reply_retry(self):
        self.native(r'''
struct ConsolePrinter:FakePrinter {
 unsigned reads=0;bool empty=true,fail=false;
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  if(path=="/rr_model?key=seqs.reply&flags=d5"){
   hp::LuaJobService::Result r;r.ok=true;r.status=200;r.body="{\"result\":21}";return r;
  }
  if(path=="/rr_reply"){
   ++reads;hp::LuaJobService::Result r;r.ok=!fail;r.status=fail?503:200;
   r.body=empty?"":"Steps/mm: X: 100.000";return r;
  }
  assert(path.startsWith("/rr_connect"));
  return FakePrinter::request(path,session,method,body,limit);
 }
};
int main(){using namespace hp;ConsolePrinter t;RepRapPrinterAdapter a;PrinterProfile p;p.mode="standalone";String error;
 assert(a.pollConsole(t,p,error)&&a.takeReply().length()==0&&t.reads==1);
 t.empty=false;t.fail=true;assert(!a.pollConsole(t,p,error)&&t.reads==2);
 t.fail=false;assert(a.pollConsole(t,p,error)&&a.takeReply()=="Steps/mm: X: 100.000"&&t.reads==3);
 assert(a.pollConsole(t,p,error)&&a.takeReply().length()==0&&t.reads==3);
 assert(t.commands==0);
}
''')

    def native(self, tail, service=False, file_jobs=False):
        json_dir = ROOT/".pio/libdeps/home_display_p4_r3_dev/ArduinoJson/src"
        if not json_dir.exists(): self.skipTest("Pinned ArduinoJson dependency required")
        parts = ["src/services/QoiDecoder.h", "src/apps/lua/LuaTickPolicy.h", "src/services/printer/PrinterSafety.h", "src/services/printer/PrinterAdapter.h",
                 "src/services/printer/PrinterFileJobs.h", "src/services/printer/PrinterThumbnailPolicy.h", "src/services/printer/RepRapPrinterAdapter.h",
                 "src/services/printer/MoonrakerPrinterAdapter.h",
                 "src/services/printer/OctoPrintPrinterAdapter.h", "src/services/printer/PrinterJson.h", "src/services/printer/PrinterComponents.h",
                 "src/services/printer/PrinterRuntimeModel.h", "src/services/printer/PrinterActionCatalog.h",
                 "src/services/printer/RepRapPrinterAdapter.cpp",
                 "src/services/printer/MoonrakerPrinterAdapter.cpp",
                 "src/services/printer/OctoPrintPrinterAdapter.cpp"]
        code = source("tests/native/printer_fake.h")+"\n"+"\n".join(stripped(x) for x in parts)
        if file_jobs:
            code += "\n"+source("tests/native/printer_files_fake.h")+"\n"+stripped("src/services/printer/PrinterFileJobs.cpp")
        if service:
            code += "\n"+source("tests/native/printer_service_fake.h")+"\n"
            code += 'namespace hp {void printerUploadHttp(LuaJobService::Request,const String&,const String&,const String&,LuaJobService::Result& r){r.error="test_upload_unavailable";}}\n'
            code += 'namespace hp {bool printerLocalThumbnail(const String&,String&,const std::atomic<bool>*,String&,unsigned,unsigned){return false;}bool printerPreviewImage(const String&,String&,const std::atomic<bool>*,String&,String&,unsigned,unsigned){return false;}}\n'
            code += 'namespace hp {void printerDiscardPreview(const char*,const String&){}}\n'
            code += 'namespace hp {String printerMultipartBoundary(){return "test-boundary";}}\n'
            code += stripped("src/services/PrinterService.h").replace("private:","public:")+"\n"+stripped("src/services/PrinterService.cpp")
        code += "\n"+source("tests/native/printer_fixture.h")+"\n"+source("tests/native/printer_modern_fixture.h")+"\n"+tail
        with tempfile.TemporaryDirectory(prefix="printer-service-") as temp:
            work=Path(temp);(work/"check.cpp").write_text(code,encoding="utf-8")
            compiler=shutil.which("g++") or shutil.which("clang++")
            if compiler: command=[compiler,"-std=c++17","-I"+str(json_dir),"check.cpp","-o","check.exe"]
            else:
                setup=Path("C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
                if not setup.exists(): self.skipTest("Native compiler unavailable")
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 /I"{json_dir}" check.cpp /Fe:check.exe"'
            r=subprocess.run(command,cwd=work,capture_output=True,text=True,timeout=60)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(work/"check.exe")],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_moonraker_limits_macros_paths_and_no_unsafe_commands(self):
        self.native(r'''
int main(){using namespace hp;ModernPrinter t;MoonrakerPrinterAdapter a;PrinterProfile p;PrinterStatus s;String error,reply,json;JsonDocument args;
 assert(a.poll(t,p,s,error)&&s.layer==2&&s.layers==10&&s.axes[2].maxFeed==300);
 args["axis"]="z";args["relative"]=true;args["distance"]=1;
 assert(a.command(t,p,"move",args,s,reply,error)&&t.lastBody.indexOf("F300.0")>=0);
 const auto count=t.posts;t.motion="Printing";
 assert(!a.command(t,p,"move",args,s,reply,error)&&t.posts==count);
 assert(!a.command(t,p,"home",args,s,reply,error)&&t.posts==count);t.motion="Ready";
 args.clear();args["heater"]="nozzle";args["target"]=200;t.extruder="extruder1";
 assert(!a.command(t,p,"set_temperature",args,s,reply,error)&&t.posts==count);t.extruder="extruder";
 assert(a.command(t,p,"set_temperature",args,s,reply,error)&&t.lastBody.indexOf("HEATER=extruder")>=0);
 args.clear();args["name"]="M112";const auto before=t.posts;
 assert(!a.command(t,p,"run_macro",args,s,reply,error)&&error=="printer_macro_not_configured"&&t.posts==before);
 args["name"]="_PRIVATE";assert(!a.command(t,p,"run_macro",args,s,reply,error)&&t.posts==before);
 args["name"]="TEST";assert(a.command(t,p,"run_macro",args,s,reply,error));
 args.clear();args["path"]="0:/gcodes/folder/test.gcode";
 assert(a.command(t,p,"start_file",args,s,reply,error)&&t.lastPath.indexOf("folder%2Ftest.gcode")>=0);
 args["path"]="0:/gcodes/../config";const auto safe=t.posts;assert(!a.command(t,p,"start_file",args,s,reply,error)&&t.posts==safe);
 assert(a.list(t,p,"0:/gcodes",json,error));JsonDocument files;deserializeJson(files,json);
 assert(files["entries"].size()==2&&files["entries"][0]["directory"].as<bool>());
 assert(a.list(t,p,"0:/gcodes/folder",json,error)&&t.lastPath.indexOf("gcodes%2Ffolder")>=0);
 assert(a.list(t,p,"0:/macros",json,error));deserializeJson(files,json);assert(files["entries"].size()==1);
 t.malformed=true;assert(!a.list(t,p,"0:/gcodes",json,error));assert(!a.poll(t,p,s,error));
}''')

    def test_octoprint_transitions_offline_directories_and_controls(self):
        self.native(r'''
int main(){using namespace hp;ModernPrinter t;t.octo=true;t.state="Operational";OctoPrintPrinterAdapter a;PrinterProfile p;PrinterStatus s;String error,reply,json;JsonDocument args;
 assert(a.poll(t,p,s,error)&&printerIdle(s)&&s.nozzle.current==205&&!s.nozzle.limit);
 assert(a.command(t,p,"home",args,s,reply,error)&&t.lastPath=="/api/printer/printhead");
 args["percent"]=110;assert(a.command(t,p,"set_speed_factor",args,s,reply,error));
 args["percent"]=250;const auto before=t.posts;assert(!a.command(t,p,"set_speed_factor",args,s,reply,error)&&t.posts==before);
 t.state="Pausing";assert(a.poll(t,p,s,error)&&!strcmp(s.state,"pausing"));
 assert(!a.command(t,p,"resume",args,s,reply,error)&&t.posts==before);
 t.state="Paused";assert(a.command(t,p,"resume",args,s,reply,error));
 t.state="Offline";assert(a.poll(t,p,s,error)&&!strcmp(s.state,"error")&&t.lastPath=="/api/job");
 t.state="Operational";assert(a.list(t,p,"0:/gcodes",json,error));JsonDocument files;deserializeJson(files,json);
 assert(files["entries"].size()==2&&files["entries"][0]["directory"].as<bool>());
 assert(a.list(t,p,"0:/gcodes/folder",json,error));
 args.clear();args["path"]="0:/gcodes/folder/test.gcode";
 assert(a.command(t,p,"start_file",args,s,reply,error)&&t.lastPath=="/api/files/local/folder%2Ftest.gcode");
 args["path"]="0:/gcodes/../config";const auto safe=t.posts;assert(!a.command(t,p,"start_file",args,s,reply,error)&&t.posts==safe);
 assert(!a.command(t,p,"move",args,s,reply,error));assert(!a.command(t,p,"set_temperature",args,s,reply,error));
 t.malformed=true;assert(!a.poll(t,p,s,error)&&!a.list(t,p,"0:/gcodes",json,error));
}''')

    def test_modern_transport_owner_scoped_keys_and_permissions(self):
        self.native(r'''
namespace hp {
LuaJobService::Request captured;
void LuaJobService::executeHttp(const Request& q,Result& r){++networkRequests;captured=q;r.ok=true;r.status=200;r.body="{}";}
}
int main(){using namespace hp;std::atomic<bool> active{true};PrinterProfile p;p.owner="owner";p.host="192.168.1.2";p.protocol="octoprint";
 const auto permissions=perm(AppPermission::Network)|perm(AppPermission::UserStorage)|perm(AppPermission::Secrets);
 WorkerPrinterTransport missing(p,permissions,active);assert(missing.request("/api/job","",0,"",1024).error=="printer_secret_missing"&&networkRequests==0);
 p.passwordSecret="printer_key";secrets["other:printer_key"]="other-value";
 WorkerPrinterTransport noForeign(p,permissions,active);assert(noForeign.request("/api/job","",0,"",1024).error=="printer_secret_missing"&&networkRequests==0);
 secrets["owner:printer_key"]="my-key";
 WorkerPrinterTransport denied(p,perm(AppPermission::Network),active);assert(denied.request("/api/job","",0,"",1024).error=="printer_secret_permission"&&networkRequests==0);
 WorkerPrinterTransport allowed(p,permissions,active);assert(allowed.request("/api/job","",0,"",1024).ok);
 assert(captured.owner=="owner"&&captured.headerCount==1&&captured.headers[0].name=="X-Api-Key"&&captured.headers[0].value=="my-key"&&captured.contentType=="application/json");
 secrets["owner:printer_key"]="bad\r\nHeader: injected";const auto before=networkRequests;
 assert(allowed.request("/api/job","",0,"",1024).error=="printer_secret_invalid"&&networkRequests==before);
 p.protocol="moonraker";p.passwordSecret="";WorkerPrinterTransport lan(p,permissions,active);assert(lan.request("/printer/info","",0,"",1024).ok&&captured.headerCount==0);
 active=false;assert(!lan.request("/printer/info","",0,"",1024).ok);
}''',service=True)

    def test_read_transport_yields_but_commands_are_not_replayed(self):
        self.native(r'''
namespace hp {void LuaJobService::executeHttp(const Request&,Result& r){++networkRequests;r.ok=true;r.status=200;r.body="{}";}}
int main(){using namespace hp;PrinterProfile p;p.owner="owner";p.host="192.168.1.2";p.protocol="reprap";
std::atomic<bool> active{true};const auto permissions=perm(AppPermission::Network)|perm(AppPermission::UserStorage);
WorkerPrinterTransport poll(p,permissions,active);poll.allowPriorityYield();
assert(poll.request("/rr_model?key=state","",0,"",1024).ok&&networkRequests==1);
queuedPriority=1;assert(!poll.request("/rr_model?key=heat","",0,"",1024).ok&&poll.yielded()&&networkRequests==1);
WorkerPrinterTransport command(p,permissions,active);assert(command.request("/rr_gcode","",1,"M25",1024).ok);
assert(!command.yielded()&&networkRequests==2);active=false;
assert(!command.request("/rr_gcode","",1,"M25",1024).ok&&networkRequests==2);
}
''',service=True)

    def test_actual_adapter_authentication_object_model_and_directories(self):
        self.native(r'''
int main(){using namespace hp;PrinterProfile p;p.protocol="reprap";p.mode="auto";FakePrinter t;RepRapPrinterAdapter a;PrinterStatus s;String error,json;
 assert(a.poll(t,p,s,error));assert(t.paths.size()==12&&t.paths[0].startsWith("/rr_connect?sessionKey=yes&password=reprap"));
 assert(t.paths[1]=="/rr_model?key=state&flags=d5"&&t.sessions[1]=="123456");assert(!strcmp(s.state,"idle"));
 assert(s.nozzle.current==205&&s.nozzle.target==210&&s.nozzle.max==285&&s.bed.current==55);
 assert(s.axisCount==3&&s.axes[0].homed&&s.fanCount==2&&s.fans[1].automatic);
 assert(s.progress==25&&s.layer==3&&s.layers==120&&s.httpMillimeters);
 auto bounded=s;bounded.axes[0].machinePosition=249;bounded.axes[0].position=100;
 assert(!printerJogSafe(bounded,'X',10));bounded.httpMillimeters=false;assert(!printerJogSafe(bounded,'Y',1));
 const String name=s.printerName,firmware=s.firmware;
 assert(a.poll(t,p,s,error));assert(t.paths.size()==21);
 assert(String(s.printerName)==name&&String(s.firmware)==firmware);
 unsigned beforeRefresh=t.paths.size();now+=30000;
 assert(a.poll(t,p,s,error));assert(t.paths.size()==beforeRefresh+12);
 assert(a.list(t,p,"0:/gcodes",json,error));JsonDocument files;assert(!deserializeJson(files,json));
 assert(files["entries"].size()==2&&files["entries"][1]["path"]=="0:/gcodes/test.gcode");
 assert(files["sort"]=="name_on_page"&&files["page_size"]==32);
 assert(a.list(t,p,"0:/macros",json,error));assert(t.paths.back().startsWith("/rr_filelist?dir=0%3A%2Fmacros"));
 a.reset();t.auth=1;t.paths.clear();error="";assert(!a.poll(t,p,s,error)&&error=="printer_authentication");assert(t.paths.size()==1);
 a.reset();t.auth=0;t.sbc=true;t.paths.clear();assert(a.poll(t,p,s,error));
 assert(t.paths.size()==3&&t.paths[1].startsWith("/machine/connect?password=")&&t.paths[2]=="/machine/model");
 assert(a.list(t,p,"0:/gcodes",json,error)&&t.paths.back().startsWith("/machine/directory/"));
 a.reset();t.sbc=false;t.invalidModel=true;assert(!a.poll(t,p,s,error)&&error=="printer_model_state_missing");
 assert(!printerIdentity("bad/id",36)&&printerIdentity("p-long-profile-123456789012345678901",36));
 assert(!printerPath("0:/gcodes/../sys/config.g","0:/gcodes/"));
 assert(!printerPath("0:/gcodes/a\"\nM112","0:/gcodes/"));
}''')

    def test_actual_commands_safety_acceptance_and_no_command_retry(self):
        self.native(r'''
int main(){using namespace hp;PrinterProfile p;p.mode="auto";FakePrinter t;RepRapPrinterAdapter a;PrinterStatus s;String error,reply;JsonDocument params;
 params["axis"]="x";params["distance"]=10;params["relative"]=true;
 assert(a.command(t,p,"move",params.as<JsonVariantConst>(),s,reply,error));
 assert(t.lastCode.find("M120%0AG91%0AG1%20X10.000%20F1200.000%0AM121")!=std::string::npos);
 unsigned before=t.commands;t.homed=false;assert(!a.command(t,p,"move",params.as<JsonVariantConst>(),s,reply,error)&&t.commands==before);
 t.homed=true;params["distance"]=11;assert(!a.command(t,p,"move",params.as<JsonVariantConst>(),s,reply,error));
 params.clear();params["heater"]="nozzle";params["target"]=286;assert(!a.command(t,p,"set_temperature",params.as<JsonVariantConst>(),s,reply,error));
 params["target"]=220;assert(a.command(t,p,"set_temperature",params.as<JsonVariantConst>(),s,reply,error));
 t.state="processing";params.clear();assert(a.command(t,p,"pause",params.as<JsonVariantConst>(),s,reply,error)&&t.lastCode=="M25");
 assert(a.command(t,p,"cancel",params.as<JsonVariantConst>(),s,reply,error)&&t.lastCode=="M25%0AM0");
 t.state="paused";assert(a.command(t,p,"resume",params.as<JsonVariantConst>(),s,reply,error)&&t.lastCode=="M24");
 t.state="idle";params["path"]="0:/gcodes/test.gcode";assert(a.command(t,p,"start_file",params.as<JsonVariantConst>(),s,reply,error));
 params["path"]="0:/gcodes/../sys/config.g";assert(!a.command(t,p,"start_file",params.as<JsonVariantConst>(),s,reply,error));
 params.clear();params["code"]="M112";assert(!a.command(t,p,"gcode",params.as<JsonVariantConst>(),s,reply,error));
 params["code"]="M115";t.failCommand=true;before=t.commands;
 assert(!a.command(t,p,"gcode",params.as<JsonVariantConst>(),s,reply,error)&&t.commands==before+1&&error=="printer_command_outcome_unknown");
 t.failCommand=false;t.sbc=true;a.reset();params.clear();assert(a.command(t,p,"home",params.as<JsonVariantConst>(),s,reply,error));
 assert(t.lastPath=="/machine/code?async=true"&&t.lastMethod==1&&t.lastBody=="G28 X Y Z");
}''')

    def test_registration_owner_scoping_and_existing_worker_only(self):
        bridge=source("src/apps/lua/PrinterLuaBridge.cpp")
        for api in ["list","snapshot","command","files","macros","console","capabilities"]: self.assertIn('"hp_printer_'+api+'"',bridge)
        self.assertIn("registerPrinterLuaBridge(L)",source("src/apps/lua/LuaApp.cpp"))
        self.assertIn("printer_background_control_denied",bridge)
        service=source("src/services/PrinterService.cpp")
        self.assertIn('luaStringForNative(o.id,"printer_profiles")',service)
        self.assertIn("backgroundWindow()",service)
        self.assertIn("foregroundLoad()==ForegroundLoad::High",service)
        self.assertIn('q.permissions=permissions_&perm(AppPermission::Network)',service)
        for path in ["src/services/PrinterService.cpp","src/services/printer/RepRapPrinterAdapter.cpp","src/apps/lua/PrinterLuaBridge.cpp"]:
            self.assertNotIn("xTaskCreate",source(path));self.assertNotIn("lv_timer_handler",source(path))
        adapter=source("src/services/printer/RepRapPrinterAdapter.cpp")
        poll=adapter[adapter.index("bool RepRapPrinterAdapter::poll"):adapter.index("bool RepRapPrinterAdapter::list")]
        self.assertNotIn("rr_gcode",poll);self.assertNotIn("M105",poll);self.assertNotIn("rr_status",adapter)
        self.assertIn("q.resource=generation",service)

    def test_actual_service_cache_profiles_background_cleanup_and_queued_commands(self):
        self.native(r'''
namespace hp {
void LuaJobService::executeHttp(const Request& q,Result& r){
 assert(workerContext&&q.permissions==perm(AppPermission::Network));
 ++networkRequests;urls.push_back(q.url);
 FakePrinter t;const int slash=q.url.indexOf('/');(void)slash;
 const size_t path=q.url.find('/',7);String session=q.headerCount?q.headers[0].value:String();
 r=t.request(q.url.substring(path,q.url.length()),session,q.method,q.body,q.responseLimit);
}}
int main(){using namespace hp;auto& s=PrinterService::instance();assert(s.begin());
 const auto permission=perm(AppPermission::Network)|perm(AppPermission::UserStorage);
 kv["owner:printer_profiles"]=R"([{"id":"one","name":"Duet","host":"192.168.1.20","port":80,"protocol":"reprap"},{"id":"two","name":"Duet2","host":"192.168.1.21","port":80,"protocol":"reprap"}])";
 workerContext=false;assert(!s.arm("invalid/owner",permission));assert(!s.arm("owner",0));assert(s.arm("owner",permission));
 assert(networkRequests==0);String initial=s.snapshot("owner","one");assert(initial.indexOf("Verbinden")>=0);
 workerContext=true;s.tick();assert(networkRequests==0);
 workerContext=false;s.snapshot("owner","one");workerContext=true;s.tick();assert(networkRequests==12);
 workerContext=false;auto cached=s.snapshot("owner","one");assert(cached.indexOf("205")>=0&&networkRequests==12);
 PrinterProfile expected;expected.owner="owner";expected.id="one";expected.host="192.168.1.20";expected.protocol="reprap";expected.mode="auto";
 const auto seen=s.owners_[0].entries[0].lastSeen;s.owners_[0].sync=false;
 assert(s.cachedSnapshot(expected)==cached&&networkRequests==12&&!s.owners_[0].sync&&s.owners_[0].entries[0].lastSeen==seen);
 expected.host="192.168.1.99";assert(!s.cachedSnapshot(expected).length());expected.host="192.168.1.20";
 expected.owner="other";assert(!s.cachedSnapshot(expected).length()&&networkRequests==12);expected.owner="owner";
 busyLock=true;assert(!s.cachedSnapshot(expected).length());busyLock=false;
 assert(s.snapshot("other","one").indexOf("205")<0);busyLock=true;assert(s.snapshot("owner","one").indexOf("printer_busy")>=0);busyLock=false;
 auto job=s.command("owner",permission,"one","home","{}");assert(job==42&&networkRequests==12);
 auto queued=LuaJobService::instance().queued;workerContext=true;LuaJobService::Result result;queued.handler(queued,result);assert(result.ok&&result.status==202);
 workerContext=false;s.cleanupOwner("owner",true);workerContext=true;now+=60000;load=ForegroundLoad::High;s.tick();
 unsigned before=networkRequests;now+=20000;s.tick();assert(networkRequests==before);
 load=ForegroundLoad::Normal;networkOnline=false;now+=20000;s.tick();assert(networkRequests==before);
 networkOnline=true;now+=20000;s.tick();now+=500;s.tick();assert(networkRequests>before);
 workerContext=false;s.cleanupOwner("owner",false);workerContext=true;now+=10000;s.tick();before=networkRequests;
 now+=120000;s.tick();assert(networkRequests==before&&s.owners_[0].count==0);
 queued.handler(queued,result);assert(result.error=="printer_profile_unavailable");
 workerContext=false;assert(s.arm("owner",permission));workerContext=true;s.tick();
 kv["owner:printer_profiles"]=R"([{"id":"one","name":"Changed","host":"192.168.1.99","port":80,"protocol":"reprap"}])";
 workerContext=false;s.profiles("owner",nullptr,true);workerContext=true;now+=1000;s.tick();now+=1000;s.tick();
 assert(urls.back().startsWith("http://192.168.1.99:80/"));
 // An already queued command must not follow a profile onto another device.
 workerContext=false;assert(s.command("owner",permission,"one","home","{}")==42);queued=LuaJobService::instance().queued;
 kv["owner:printer_profiles"]=R"([{"id":"one","name":"Changed","host":"192.168.1.100","port":80,"protocol":"reprap"}])";
 workerContext=true;before=networkRequests;result={};queued.handler(queued,result);
 assert(!result.ok&&result.error=="printer_profile_changed"&&networkRequests==before);
}''',service=True)

if __name__=="__main__": unittest.main()
