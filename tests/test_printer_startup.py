import unittest
from pathlib import Path
import test_printer_service as printer_tests

ROOT=Path(__file__).resolve().parents[1]

class PrinterStartupTests(unittest.TestCase):
    def test_directory_does_not_wait_for_valid_model(self):
        printer_tests.PrinterServiceTests().native(r'''
namespace hp {
void LuaJobService::executeHttp(const Request& q,Result& r){
 ++networkRequests;r.ok=true;r.status=200;
 if(q.url.indexOf("/rr_connect")>=0)r.body=R"({"err":0,"sessionKey":"123","sessionTimeout":8000})";
 else if(q.url.indexOf("/rr_filelist")>=0)r.body=R"({"files":[{"name":"part.gcode","type":"f","size":64}],"next":0})";
 else assert(false && "metadata must not require a model poll or send G-code");
}
}
int main(){using namespace hp;auto& s=PrinterService::instance();assert(s.begin());
 const auto permissions=perm(AppPermission::Network)|perm(AppPermission::UserStorage);
 kv["owner:printer_profiles"]=R"([{"id":"p1","host":"192.168.178.211","protocol":"reprap","background":false}])";
 workerContext=false;assert(s.arm("owner",permissions));workerContext=true;s.tick();
 auto* o=s.findOwner("owner");auto* e=s.findEntry(o,"p1");assert(e && !e->status->valid);
 workerContext=false;s.snapshot("owner","p1",true);
 JsonDocument d;assert(!deserializeJson(d,s.files("owner","p1","/")) && d["pending"]==true);
 load=ForegroundLoad::High;workerContext=true;s.nextNetwork_=0;e->due=0;s.tick();
 workerContext=false;d.clear();assert(!deserializeJson(d,s.files("owner","p1","/")));
 assert(d["pending"]==false && d["entries"].size()==1 && !e->status->valid);
 // No status buffer is required merely to list a folder.
 e->status.reset();s.files("owner","p1","/",false,true);workerContext=true;e->due=0;s.nextNetwork_=0;s.tick();
 workerContext=false;d.clear();assert(!deserializeJson(d,s.files("owner","p1","/")) && d["pending"]==false);
 e->profile.enabled=false;d.clear();deserializeJson(d,s.files("owner","p1","/"));assert(d["pending"]==false && d["error"]=="printer_profile_disabled");
 d.clear();deserializeJson(d,s.files("other","p1","/"));assert(d["pending"]==false && d["error"]=="printer_profile_unavailable");
 e->profile.enabled=true;e->files="";networkOnline=false;d.clear();deserializeJson(d,s.files("owner","p1","/"));assert(d["pending"]==false && d["error"]=="network_offline");
}''',service=True)

    def test_profile_cache_loading_errors_and_worker_metadata(self):
        printer_tests.PrinterServiceTests().native(r'''
namespace hp {void LuaJobService::executeHttp(const Request&,Result&){assert(false);}}
int main(){using namespace hp;auto& service=PrinterService::instance();assert(service.begin());
 const auto permissions=perm(AppPermission::Network)|perm(AppPermission::UserStorage);
 kv["owner:printer_profiles"]=R"([{"id":"p1","host":"192.168.178.211","protocol":"mainsail","enabled":false,"background":false}])";
 kv["owner:selected_printer"]="p1";
 workerContext=false;assert(service.arm("owner",permissions));String error;
 assert(service.profiles("owner",&error).empty()&&error=="printer_profiles_loading");
 workerContext=true;service.tick();workerContext=false;
 const auto cached=service.profiles("owner",&error);JsonDocument d;
 assert(error.empty()&&!deserializeJson(d,cached));
 assert(d[0]["protocol"]=="moonraker"&&d[0]["port"]==7125&&d[0]["enabled"]==false&&d[0]["background"]==false&&d[0]["selected"]==true);
 assert(networkRequests==0);busyLock=true;
 assert(service.profiles("owner",&error).empty()&&error=="printer_busy");busyLock=false;
 kv["owner:printer_profiles"]="broken";service.profiles("owner",&error,true);workerContext=true;service.tick();workerContext=false;
 assert(service.profiles("owner",&error)==cached);
 JsonDocument state;assert(!deserializeJson(state,service.profileState("owner"))&&state["error"]=="printer_profiles_invalid");
 kv["owner:printer_profiles"]="[]";service.profiles("owner",&error,true);workerContext=true;service.tick();workerContext=false;
 assert(service.profiles("owner",&error)=="[]"&&error.empty());
}''',service=True)

    def test_dropdown_options_are_guarded_and_startup_has_no_nvs_reads(self):
        ui=(ROOT/'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8')
        widgets=(ROOT/'src/apps/lua/LuaWidgetsBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('guard(L,"hp_ui_dropdown_set_options",true)',ui)
        self.assertIn('lua_register(L,"hp_ui_dropdown_set_options",dropdownOptions)',widgets)
