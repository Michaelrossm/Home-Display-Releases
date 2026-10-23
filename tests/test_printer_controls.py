import unittest
import test_printer_service as printer


class PrinterControlTests(unittest.TestCase):
    def native(self, text):
        printer.PrinterServiceTests().native(text)

    def test_klipper_configured_calibrations_and_no_replay(self):
        self.native(r'''
struct ControlKlipper:ModernPrinter {
 bool configured=true,homed=true,hot=true,unknown=false;
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  auto r=ModernPrinter::request(path,session,method,body,limit);
  if(method&&unknown){r.ok=false;r.status=0;r.error="timeout";return r;}
  if(path.startsWith("/printer/objects/query")){
   JsonDocument d;assert(!deserializeJson(d,r.body));auto m=d["result"]["status"];
   if(configured)for(const char* key:{"bed_mesh","z_tilt","quad_gantry_level","screws_tilt_adjust"})m["configfile"]["settings"][key].to<JsonObject>();
   m["toolhead"]["homed_axes"]=homed?"xyz":"xy";m["extruder"]["can_extrude"]=hot;
   r.body="";serializeJson(d,r.body);
  }return r;
 }
};
int main(){using namespace hp;ControlKlipper t;MoonrakerPrinterAdapter a;PrinterProfile p;PrinterStatus s;JsonDocument args;String reply,error;
 struct Action{const char* name;const char* code;};
 for(auto op:{Action{"bed_mesh","BED_MESH_CALIBRATE"},{"z_tilt","Z_TILT_ADJUST"},{"gantry_level","QUAD_GANTRY_LEVEL"},{"screws_tilt","SCREWS_TILT_CALCULATE"},{"mesh_clear","BED_MESH_CLEAR"}}){
  assert(a.command(t,p,op.name,args,s,reply,error)&&t.lastBody.indexOf(op.code)>=0);
  const auto n=t.posts;t.homed=false;assert(!a.command(t,p,op.name,args,s,reply,error)&&t.posts==n);t.homed=true;
 }
 t.configured=false;const auto n=t.posts;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.posts==n);
 t.configured=true;t.state="printing";assert(!a.command(t,p,"z_tilt",args,s,reply,error)&&t.posts==n);t.state="standby";
 args["tool"]="extruder";assert(a.command(t,p,"select_tool",args,s,reply,error)&&t.lastBody.indexOf("ACTIVATE_EXTRUDER EXTRUDER=extruder")>=0);
 args["distance"]=5;args["feed"]=60;assert(a.command(t,p,"extrude",args,s,reply,error)&&t.lastBody.indexOf("RESTORE_GCODE_STATE")>=0);
 auto before=t.posts;t.hot=false;assert(!a.command(t,p,"extrude",args,s,reply,error)&&t.posts==before);t.hot=true;
 args["distance"]=11;assert(!a.command(t,p,"extrude",args,s,reply,error)&&t.posts==before);
 args["tool"]="extruder\nM112";assert(!a.command(t,p,"select_tool",args,s,reply,error)&&t.posts==before);
 args["tool"]="extruder";args["percent"]=95;assert(a.command(t,p,"set_flow_factor",args,s,reply,error)&&t.lastBody.indexOf("M221")>=0);
 t.unknown=true;before=t.posts;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.posts==before+1&&error=="printer_command_outcome_unknown");
}
''')

    def test_reprap_grid_probe_tool_and_extrusion_guards(self):
        self.native(r'''
struct ControlDuet:FakePrinter {
 bool grid=true,probe=true,hot=true;
 hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)override{
  auto r=FakePrinter::request(path,session,method,body,limit);
  if(path.startsWith("/rr_model")||path=="/machine/model"){
   JsonDocument d;assert(!deserializeJson(d,r.body));auto m=sbc?d.as<JsonVariant>():d["result"].as<JsonVariant>();
   JsonVariant heat=sbc?m["heat"].as<JsonVariant>():path.indexOf("key=heat")>=0?m:JsonVariant();
   if(!heat.isNull()){heat["coldExtrudeTemperature"]=170;heat["heaters"][1]["current"]=hot?205:20;}
   JsonVariant move=sbc?m["move"].as<JsonVariant>():path.indexOf("key=move")>=0?m:JsonVariant();
   if(!move.isNull()){move["extruders"][0]["position"]=0;if(grid){auto g=move["compensation"]["probeGrid"];g["axes"][0]="X";g["axes"][1]="Y";for(int i=0;i<2;++i){g["mins"][i]=20;g["maxs"][i]=200;g["spacings"][i]=30;}}}
   if(sbc)m["sensors"]["probes"][0]["type"]=probe?9:0;
   else if(path.indexOf("key=sensors")>=0)m["probes"][0]["type"]=probe?9:0;
   JsonVariant tools=sbc?m["tools"].as<JsonVariant>():path.indexOf("key=tools")>=0?m:JsonVariant();if(!tools.isNull())tools[0]["extruders"][0]=0;
   r.body="";serializeJson(d,r.body);
  }return r;
 }
};
int main(){using namespace hp;for(bool sbc:{false,true}){ControlDuet t;t.sbc=sbc;RepRapPrinterAdapter a;PrinterProfile p;p.mode=sbc?"sbc":"standalone";PrinterStatus s;JsonDocument args;String reply,error;
 assert(a.command(t,p,"bed_mesh",args,s,reply,error)&&t.lastCode.indexOf("G29")>=0);
 auto before=t.commands;t.homed=false;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.commands==before);t.homed=true;
 t.probe=false;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.commands==before);t.probe=true;
 t.grid=false;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.commands==before);t.grid=true;
 assert(!a.command(t,p,"z_tilt",args,s,reply,error)&&t.commands==before);p.alignment="rrf_bed_g";
 assert(a.command(t,p,"z_tilt",args,s,reply,error)&&t.lastCode=="G32");
 args["tool"]="tool_0";assert(a.command(t,p,"select_tool",args,s,reply,error)&&t.lastCode=="T0");
 args["distance"]=-5;args["feed"]=60;assert(a.command(t,p,"extrude",args,s,reply,error)&&t.lastCode.indexOf("M83")>=0);
 before=t.commands;t.hot=false;assert(!a.command(t,p,"extrude",args,s,reply,error)&&t.commands==before);t.hot=true;
 args["percent"]=95;assert(a.command(t,p,"set_flow_factor",args,s,reply,error)&&t.lastCode.indexOf("M221")>=0);
 t.failCommand=true;before=t.commands;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.commands==before+1);
 }}
''')

    def test_marlin_requires_explicit_leveling_profile(self):
        self.native(r'''
int main(){using namespace hp;ModernPrinter t;t.octo=true;t.state="Operational";OctoPrintPrinterAdapter a;PrinterProfile p;PrinterStatus s;JsonDocument args;String reply,error;
 assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.posts==0);
 p.calibration="marlin_bilinear";assert(a.command(t,p,"bed_mesh",args,s,reply,error)&&t.lastBody=="{\"commands\":[\"G28\",\"G29\"]}");
 p.calibration="marlin_ubl";assert(a.command(t,p,"bed_mesh",args,s,reply,error)&&t.lastBody.indexOf("G29 P1")>=0);
 auto before=t.posts;assert(!a.command(t,p,"z_tilt",args,s,reply,error)&&t.posts==before);p.alignment="marlin_g34";
 assert(a.command(t,p,"z_tilt",args,s,reply,error)&&t.lastBody.indexOf("G34")>=0);
 t.state="Printing";before=t.posts;assert(!a.command(t,p,"bed_mesh",args,s,reply,error)&&t.posts==before);t.state="Operational";
 args["tool"]="tool0";assert(a.command(t,p,"select_tool",args,s,reply,error)&&t.lastPath=="/api/printer/tool");
 args["tool"]="tool9";before=t.posts;assert(!a.command(t,p,"select_tool",args,s,reply,error)&&t.posts==before);
 assert(!a.command(t,p,"extrude",args,s,reply,error)&&t.posts==before);
}
''')

    def test_policy_has_no_implicit_save_or_unbounded_extrusion(self):
        self.native(r'''
int main(){using namespace hp;PrinterStatus s;strlcpy(s.state,"idle",sizeof(s.state));s.valid=true;s.canExtrude=true;s.httpChannelIdle=true;s.httpMillimeters=true;
 assert(printerExtrusionSafe(s,5,60));assert(!printerExtrusionSafe(s,11,60));assert(!printerExtrusionSafe(s,5,301));
 s.canExtrude=false;assert(!printerExtrusionSafe(s,-5,60));
 assert(!printerAllHomed(s));for(int i=0;i<3;++i){s.axes[i].letter="XYZ"[i];s.axes[i].homed=true;s.axes[i].homedKnown=true;}s.axisCount=3;
 assert(printerCalibrationSafe(s));s.axes[2].homedKnown=false;assert(!printerCalibrationSafe(s));
 for(const char* name:{"bed_mesh","z_tilt","select_tool","extrude","motors_off"})assert(printerCommandQueueExpired(name,100,10101));
 assert(!printerCalibrationProfile("G29\nM112")&&!printerAlignmentProfile("G32\nM112"));
 JsonDocument d;printerWriteControls(d.to<JsonObject>(),s);assert(d["z_tilt"]==false);
}
''')


if __name__ == '__main__':
    unittest.main()
