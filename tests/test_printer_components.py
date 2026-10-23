import unittest
import test_printer_service as printer_tests
source = printer_tests.source


class PrinterComponentsTests(unittest.TestCase):
    def native(self, body, service=False):
        printer_tests.PrinterServiceTests().native(body, service=service)

    def test_rrf_sensor_dedup_mapping_limits_and_nulls(self):
        self.native(r'''
int main(){using namespace hp;JsonDocument model;String error;
 assert(!deserializeJson(model,R"({"state":{"status":"idle","currentTool":0},"heat":{"bedHeaterMapping":[[0]],"chamberHeaterMapping":[[2]],"heaters":[{"current":50,"active":60,"sensor":0,"max":120,"state":"active"},{"current":200,"active":210,"sensor":1,"max":285,"state":"active"},{"current":35,"sensor":2,"max":80,"state":"off"}]},"sensors":{"analog":[{"name":"Bett","lastReading":50,"state":"ok"},{"name":"Düse","lastReading":200,"state":"ok"},{"name":"Kammer","lastReading":null,"state":"timeout"},{"name":"Umgebung","lastReading":23,"state":"ok"},{"type":"bmehumidity","lastReading":50}],"digital":[{"name":"Digital","temperature":24}]},"tools":[{"number":0,"heaters":[1],"extruders":[0]}],"move":{"extruders":[{"position":2,"factor":0.95}]}})"));
 auto s=printerStatusBuffer();assert(s&&RepRapPrinterAdapter::normalize(model,*s,error));
 assert(s->temperatureCount==5&&s->heaterCount==3&&s->toolCount==1&&s->extruderCount==1);
 assert(s->nozzle.current==200&&s->bed.current==50&&s->heaters[2].category==PrinterTemperatureKind::Chamber);
 assert(!std::isfinite(s->temperatures[2].current)&&s->heaters[2].fault&&!printerHeatSafe(*s,s->heaters[2],40,true));
 assert(!s->temperatures[3].controllable&&s->temperatures[3].heater<0);
 model["heat"]["heaters"][2]["sensor"]=1;assert(RepRapPrinterAdapter::normalize(model,*s,error));
 assert(s->temperatureCount==5&&s->temperatures[1].shared&&!s->temperatures[1].controllable&&!std::isfinite(s->temperatures[1].target));
 model["tools"][0]["heaters"].to<JsonArray>().add(1);model["tools"][0]["heaters"].add(2);
 assert(RepRapPrinterAdapter::normalize(model,*s,error)&&s->nozzle.index==-1);
 auto analog=model["sensors"]["analog"].to<JsonArray>();for(int i=0;i<40;++i){auto v=analog.add<JsonObject>();v["lastReading"]=i;v["state"]="ok";}
 assert(RepRapPrinterAdapter::normalize(model,*s,error)&&s->truncated&&s->temperatureCount==s->kTemperatures);
 JsonDocument result;printerWriteComponents(result.to<JsonObject>(),*s);assert(result["schema_version"]==2&&result["temperatures"].size()==24&&result["temperatures"][0]["current"].as<double>()==0);
 model["state"].clear();assert(!RepRapPrinterAdapter::normalize(model,*s,error));
 JsonDocument rejected;String oversized;oversized.assign(65537,' ');assert(!printerDecodeModel(oversized,rejected,error)&&error=="printer_invalid_model_or_limit");
 assert(printerDefaultPort("klipper")==7125&&printerDefaultPort("octoprint")==5000&&printerDefaultPort("rrf")==80);
}''')

    def test_rrf_real_transport_standalone_sbc_expiry_and_sensors(self):
        self.native(r'''
struct ManyDuet:FakePrinter {
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  auto r=FakePrinter::request(path,session,method,body,limit);
  if(path.startsWith("/rr_model")||path=="/machine/model"){
   JsonDocument d;assert(!deserializeJson(d,r.body));auto m=sbc?d.as<JsonVariant>():d["result"].as<JsonVariant>();
   JsonVariant sensors=sbc?m["sensors"].as<JsonVariant>():path.indexOf("key=sensors")>=0?m:JsonVariant();
   if(!sensors.isNull()){
    sensors["analog"][0]["name"]="Bett";sensors["analog"][0]["lastReading"]=55;
    sensors["analog"][1]["lastReading"]=205;sensors["analog"][2]["lastReading"]=23;
   }
   JsonVariant heat=sbc?m["heat"].as<JsonVariant>():path.indexOf("key=heat")>=0?m:JsonVariant();
   if(!heat.isNull()){heat["heaters"][0]["sensor"]=0;heat["heaters"][1]["sensor"]=1;heat["heaters"][1]["state"]="standby";}
   if(sbc)m["state"]["currentTool"]=-1;
   else if(path.indexOf("key=state")>=0)m["currentTool"]=-1;
   r.body="";serializeJson(d,r.body);
  }return r;
 }
};
int main(){using namespace hp;for(bool sbc:{false,true}){ManyDuet t;t.sbc=sbc;PrinterProfile p;p.mode=sbc?"sbc":"standalone";RepRapPrinterAdapter a;String error,reply;auto s=printerStatusBuffer();
 assert(a.poll(t,p,*s,error)&&s->temperatureCount==3&&s->nozzle.current==205);
 const auto before=t.paths.size();assert(a.poll(t,p,*s,error)&&t.paths.size()==before+(sbc?1:9));
 now+=10000;const auto expired=t.paths.size();assert(a.poll(t,p,*s,error));if(!sbc)assert(t.paths[expired].startsWith("/rr_connect"));
 JsonDocument args;args["heater"]="analog_2";args["target"]=20;const auto commands=t.commands;
 assert(!a.command(t,p,"set_temperature",args,*s,reply,error)&&t.commands==commands);
 args["heater"]="heater_1";args["target"]=220;assert(a.command(t,p,"set_temperature",args,*s,reply,error));
 assert(t.lastCode.indexOf("M568")>=0 && t.lastCode.indexOf("A2")>=0);
 args["target"]=0;assert(a.command(t,p,"set_temperature",args,*s,reply,error)&&t.lastCode.indexOf("A0")>=0);
 args["heater"]="bed";args["target"]=60;assert(a.command(t,p,"set_temperature",args,*s,reply,error)&&t.lastCode.indexOf("M140")>=0);
 args["heater"]="nozzle";args["target"]=220;assert(a.command(t,p,"set_temperature",args,*s,reply,error)&&t.lastCode.indexOf("M568")>=0);
 const auto beforeUnsafe=t.commands;args["target"]=999;assert(!a.command(t,p,"set_temperature",args,*s,reply,error)&&t.commands==beforeUnsafe);
 }}''')

    def test_moonraker_dynamic_discovery_cache_change_and_typed_control(self):
        self.native(r'''
struct ManyKlipper:ModernPrinter {
 unsigned lists=0,queries=0;bool changed=false,missing=false;
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  auto r=ModernPrinter::request(path,session,method,body,limit);if(method||!r.ok)return r;
  JsonDocument d;assert(!deserializeJson(d,r.body));
  if(path=="/printer/objects/list"){++lists;auto objects=d["result"]["objects"].as<JsonArray>();for(const char* name:{"extruder1","heater_generic chamber","temperature_sensor ambient","temperature_host cpu","temperature_fan enclosure","fan_generic auxiliary"})objects.add(name);if(changed)objects.add("temperature_sensor added");}
  else if(path.startsWith("/printer/objects/query")){++queries;auto m=d["result"]["status"].as<JsonObject>();m["heaters"]["available_sensors"].to<JsonArray>().add(changed?"changed":"original");
   m["extruder1"]["temperature"]=190;m["extruder1"]["target"]=200;m["configfile"]["settings"]["extruder1"]["max_temp"]=280;
   m["heater_generic chamber"]["temperature"]=30;m["heater_generic chamber"]["target"]=35;m["configfile"]["settings"]["heater_generic chamber"]["max_temp"]=80;
   m["temperature_sensor ambient"]["temperature"]=22;if(missing)m["temperature_sensor ambient"]["temperature"]=nullptr;
   m["temperature_host cpu"]["temperature"]=45;m["temperature_fan enclosure"]["temperature"]=30;m["temperature_fan enclosure"]["speed"]=0.4;
   m["fan"]["speed"]=0.5;m["fan_generic auxiliary"]["speed"]=0.2;
   if(changed&&path.indexOf("temperature_sensor%20added")>=0)m["temperature_sensor added"]["temperature"]=21;
  }r.body="";serializeJson(d,r.body);return r;
 }
};
int main(){using namespace hp;ManyKlipper t;MoonrakerPrinterAdapter a;PrinterProfile p;auto s=printerStatusBuffer();String error,reply;JsonDocument args;
 assert(a.poll(t,p,*s,error)&&t.lists==1&&s->temperatureCount==7&&s->toolCount==2&&s->heaterCount==4);
 assert(a.poll(t,p,*s,error)&&t.lists==1&&t.queries==2);
 args["heater"]="extruder1";args["target"]=220;assert(a.command(t,p,"set_temperature",args,*s,reply,error)&&t.lastBody.indexOf("HEATER=extruder1")>=0);
 args["heater"]="temperature_sensor ambient";const auto before=t.posts;assert(!a.command(t,p,"set_temperature",args,*s,reply,error)&&t.posts==before);
 t.changed=true;assert(a.poll(t,p,*s,error)&&t.lists==1);assert(a.poll(t,p,*s,error)&&t.lists==2&&s->temperatureCount==8);
 t.missing=true;assert(a.poll(t,p,*s,error));bool found=false;for(size_t i=0;i<s->temperatureCount;++i)if(!strcmp(s->temperatures[i].id,"temperature_sensor ambient")){found=true;assert(!std::isfinite(s->temperatures[i].current));}assert(found);
 t.malformed=true;assert(!a.poll(t,p,*s,error));
}''')

    def test_octoprint_all_standard_channels_missing_values_no_fake_limits(self):
        self.native(r'''
struct ManyOcto:ModernPrinter {
 unsigned queries=0;
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  auto r=ModernPrinter::request(path,session,method,body,limit);++queries;
  if(path=="/api/printer"){JsonDocument d;assert(!deserializeJson(d,r.body));d["temperature"]["tool1"]["actual"]=nullptr;d["temperature"]["tool1"]["target"]=nullptr;d["temperature"]["bed"]["actual"]=55;d["temperature"]["chamber"]["actual"]=30;d["temperature"]["history"].to<JsonArray>().add(99);r.body="";serializeJson(d,r.body);}return r;
 }
};
int main(){using namespace hp;ManyOcto t;t.octo=true;t.state="Operational";OctoPrintPrinterAdapter a;PrinterProfile p;auto s=printerStatusBuffer();String error,reply;JsonDocument args;
 assert(a.poll(t,p,*s,error)&&t.queries==2&&s->temperatureCount==4&&s->toolCount==2&&!std::isfinite(s->temperatures[1].current));
 for(size_t i=0;i<s->temperatureCount;++i)assert(!s->temperatures[i].controllable);
 assert(s->axisAvailability==PrinterAvailability::Unsupported&&s->fanAvailability==PrinterAvailability::Unsupported);
 args["heater"]="tool1";args["target"]=220;assert(!a.command(t,p,"set_temperature",args,*s,reply,error)&&t.posts==0);
 t.state="Offline";assert(a.poll(t,p,*s,error)&&!strcmp(s->state,"error")&&s->temperatureCount==0);
}''')

    def test_service_five_profiles_disabled_backoff_unchanged_revision_and_budget(self):
        self.native(r'''
namespace hp {void LuaJobService::executeHttp(const Request& q,Result& r){assert(workerContext);++networkRequests;FakePrinter t;const auto path=q.url.find('/',7);r=t.request(q.url.substring(path),q.headerCount?q.headers[0].value:String(),q.method,q.body,q.responseLimit);}}
int main(){using namespace hp;auto& service=PrinterService::instance();assert(service.begin());const auto permission=perm(AppPermission::Network)|perm(AppPermission::UserStorage);
 kv["owner:printer_profiles"]=R"([{"id":"one","host":"duet.local","protocol":"rrf"},{"id":"two","host":"duet.local","protocol":"rrf"},{"id":"three","host":"duet.local","protocol":"rrf"},{"id":"four","host":"duet.local","protocol":"rrf"},{"id":"off","host":"duet.local","protocol":"rrf","enabled":false}])";
 assert(service.arm("owner",permission));workerContext=true;service.tick();assert(service.owners_[0].count==5);
 for(unsigned i=0;i<20;++i){now+=1000;service.tick();}assert(networkRequests>0);auto& o=service.owners_[0];auto* e=service.findEntry(&o,"one");auto* off=service.findEntry(&o,"off");assert(e&&off&&!off->revision&&!service.command("owner",permission,"off","home","{}"));
 const auto revision=e->revision;for(unsigned i=0;i<30;++i){now+=1000;service.tick();}assert(e->revision==revision&&e->httpRequests>2);
 JsonDocument d;assert(!deserializeJson(d,e->snapshot)&&d["schema_version"]==2&&d["nozzle"]["current"]==205);
 const auto requests=d["diagnostics"]["last_poll_requests"].as<unsigned>();assert(requests==10||requests==12);
 networkOnline=false;now+=5000;service.tick();assert(e->revision>revision);const auto before=networkRequests;now+=10000;service.tick();assert(networkRequests==before);
 assert(printerBackoff(1)==5000&&printerBackoff(2)==10000&&printerBackoff(5)==40000);
 networkOnline=true;workerContext=false;assert(service.command("owner",permission,"one","home","{}"));assert(!service.command("owner",permission,"one","home","{}"));
 auto max=printerStatusBuffer();max->valid=true;strlcpy(max->state,"idle",sizeof(max->state));max->temperatureCount=24;max->heaterCount=16;max->fanCount=16;max->axisCount=12;max->toolCount=8;max->extruderCount=8;
 for(size_t i=0;i<24;++i){snprintf(max->temperatures[i].id,64,"temperature_sensor_channel_%u",unsigned(i));memset(max->temperatures[i].name,'N',47);}
 for(size_t i=0;i<16;++i){snprintf(max->heaters[i].id,64,"heater_%u",unsigned(i));snprintf(max->fans[i].id,64,"fan_%u",unsigned(i));}
 for(size_t i=0;i<8;++i){max->tools[i].heaterCount=8;max->tools[i].extruderCount=8;}
 e->status=std::move(max);String encoded=service.makeSnapshot(*e,"");assert(encoded.length()<32768&&encoded.indexOf("printer_snapshot_limit")<0&&encoded.indexOf("null")>=0);
 // Reusing an owner slot must not reuse another app's authenticated session.
 service.cleanupOwner("owner",false);kv["newowner:printer_profiles"]=kv["owner:printer_profiles"];
 assert(service.arm("newowner",permission));workerContext=true;
 auto* fresh=service.findOwner("newowner");assert(fresh&&fresh->count==0&&fresh->profiles.empty());
 service.loadProfiles(*fresh);
 auto* newOwner=service.findOwner("newowner");assert(newOwner&&newOwner->entries[0].profile.owner=="newowner"&&!newOwner->entries[0].revision&&newOwner->entries[0].snapshot.empty());
}''', service=True)

    def test_no_extra_tasks_or_lvgl_and_one_cache(self):
        for name in ("RepRap", "Moonraker", "OctoPrint"):
            code = source(f"src/services/printer/{name}PrinterAdapter.cpp")
            self.assertNotIn("lv_obj_", code)
            self.assertNotIn("xTaskCreate", code)
        self.assertIn("pushLuaJsonBounded", source("src/apps/lua/PrinterLuaBridge.cpp"))
        self.assertIn("cachedSnapshot(profile)", source("src/services/AppWebSettingsService.cpp"))
        self.assertIn("MALLOC_CAP_SPIRAM", source("src/services/printer/PrinterComponents.h"))


if __name__ == "__main__":
    unittest.main()
