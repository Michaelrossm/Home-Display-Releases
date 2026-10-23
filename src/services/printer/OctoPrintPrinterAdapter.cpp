#include "OctoPrintPrinterAdapter.h"
#include "PrinterJson.h"
#include "PrinterComponents.h"
#include <algorithm>
#include <cmath>
namespace hp { namespace {
String failure(const LuaJobService::Result& r){
 if(r.status==401||r.status==403)return "printer_authentication";
 if(r.status==404)return "printer_octoprint_endpoint_missing";
 return r.error.length()?r.error:String("printer_http_")+r.status;
}
void statusString(PrinterStatus& s,const char* state){strlcpy(s.state,state,sizeof(s.state));s.valid=true;}
}
String OctoPrintPrinterAdapter::encode(const String& s){return printerEncode(s);}
bool OctoPrintPrinterAdapter::fetch(PrinterTransport& t,const String& path,JsonDocument& doc,String& error,size_t limit){
 auto r=t.request(path,"",0,"",limit);if(!r.ok){error=failure(r);return false;}
 return printerDecodeModel(r.body,doc,error,12);
}
bool OctoPrintPrinterAdapter::send(PrinterTransport& t,const String& path,const String& body,String& error){
 if(t.stopped()){error="cancelled";return false;}
 auto r=t.request(path,"",1,body,2048);if(!r.ok){error=r.status==0?"printer_command_outcome_unknown":failure(r);return false;}
 return true;
}
bool OctoPrintPrinterAdapter::poll(PrinterTransport& t,const PrinterProfile& profile,PrinterStatus& out,String& error){
 PrinterJsonAllocator pool;JsonDocument job(&pool),printer(&pool);
 if(!fetch(t,"/api/job",job,error,16384))return false;
 auto buffer=printerStatusBuffer();if(!buffer){error="printer_memory";return false;}auto& s=*buffer;const String state=job["state"]|"";
 s.axisAvailability=PrinterAvailability::Unsupported;s.fanAvailability=PrinterAvailability::Unsupported;s.toolAvailability=PrinterAvailability::Absent;s.extruderAvailability=PrinterAvailability::Unsupported;
 if(state=="Printing")statusString(s,"printing");
 else if(state=="Paused")statusString(s,"paused");
 else if(state=="Pausing")statusString(s,"pausing");
 else if(state=="Operational")statusString(s,"idle");
 else if(state=="Cancelling")statusString(s,"cancelling");
 else if(state=="Error"||state=="Offline"||state=="Closed")statusString(s,"error");
 else if(state=="Starting")statusString(s,"starting");
 else if(state=="Finishing")statusString(s,"finishing");
 else if(state=="Connecting")statusString(s,"connecting");
 else{error="printer_model_state_missing";return false;}
 // Offline /api/printer returns 409; retain the valid connection diagnostic.
 if(!strcmp(s.state,"error")||!strcmp(s.state,"connecting")){out=s;return true;}
 if(!fetch(t,"/api/printer",printer,error,16384))return false;
 if(!printer["state"]["flags"].is<JsonObjectConst>()){error="printer_model_state_missing";return false;}
 const auto flags=printer["state"]["flags"];
 if(flags["error"].as<bool>()||flags["closedOrError"].as<bool>())statusString(s,"error");
 else if(!flags["operational"].as<bool>())statusString(s,"connecting");
 else if(flags["pausing"].as<bool>())statusString(s,"pausing");
 else if(flags["cancelling"].as<bool>())statusString(s,"cancelling");
 else if((printerIdle(s)&&(flags["printing"].as<bool>()||flags["paused"].as<bool>()||!flags["ready"].as<bool>()))||
         (!strcmp(s.state,"printing")&&!flags["printing"].as<bool>())||
         (!strcmp(s.state,"paused")&&!flags["paused"].as<bool>()))statusString(s,"transitioning");
 const char* file=job["job"]["file"]["name"]|"";strlcpy(s.filename,file,sizeof(s.filename));
 s.progress=printerNumber(job["progress"]["completion"]);s.duration=printerNumber(job["progress"]["printTime"]);
 s.remaining=printerNumber(job["progress"]["printTimeLeft"]);
 auto temp=printer["temperature"];
 if(!temp["tool0"].isNull()){
  s.nozzle.index=0;s.nozzle.tool=0;s.nozzle.current=printerNumber(temp["tool0"]["actual"]);s.nozzle.target=printerNumber(temp["tool0"]["target"]);
 }
 if(!temp["bed"].isNull()){
  s.bed.index=0;s.bed.category=PrinterTemperatureKind::Bed;s.bed.current=printerNumber(temp["bed"]["actual"]);s.bed.target=printerNumber(temp["bed"]["target"]);
 }
 s.temperatureAvailability=temp.is<JsonObjectConst>()?PrinterAvailability::Absent:PrinterAvailability::Unavailable;
 for(JsonPairConst item:temp.as<JsonObjectConst>()){
  const String name=item.key().c_str();int toolNumber=0;const bool tool=printerIndexedName(name.c_str(),"tool",toolNumber);
  if(!tool&&name!="bed"&&name!="chamber")continue;
  const auto category=tool?PrinterTemperatureKind::Hotend:name=="bed"?PrinterTemperatureKind::Bed:PrinterTemperatureKind::Chamber;
  int heaterIndex=-1;if(s.heaterCount<s.kHeaters){heaterIndex=s.heaterCount;auto& h=s.heaters[s.heaterCount++];h.index=heaterIndex;h.current=printerNumber(item.value()["actual"]);h.target=printerNumber(item.value()["target"]);h.category=category;strlcpy(h.id,name.c_str(),sizeof(h.id));strlcpy(h.name,name.c_str(),sizeof(h.name));}else s.truncated=true;
  printerTemperature(s,name.c_str(),name.c_str(),category,printerNumber(item.value()["actual"]),printerNumber(item.value()["target"]),heaterIndex,false);
  if(tool){if(s.toolCount<s.kTools){auto& t=s.tools[s.toolCount++];t.number=toolNumber;strlcpy(t.id,name.c_str(),sizeof(t.id));strlcpy(t.name,name.c_str(),sizeof(t.name));if(heaterIndex>=0){t.heaterCount=1;t.heaters[0]=heaterIndex;}}else s.truncated=true;}
 }
 if(s.temperatureCount)s.temperatureAvailability=PrinterAvailability::Available;if(s.toolCount)s.toolAvailability=PrinterAvailability::Available;
 if(s.toolCount)s.controls|=PrinterToolSelect;
 s.knownControls=511;s.configurableControls=PrinterMesh|PrinterZTilt;
 s.calibrationHomes=true;
 if(profile.calibration=="marlin_bilinear"||profile.calibration=="marlin_ubl")s.controls|=PrinterMesh;
 if(profile.alignment=="marlin_g34")s.controls|=PrinterZTilt;
 s.storageKnown=printer["sd"]["ready"].is<bool>();s.storageReady=printer["sd"]["ready"]|false;
 // OctoPrint does not expose authoritative homing/soft-limits/heat maximums
 // through these endpoints. Do not invent them: jogging and heating remain
 // disabled by shared safety checks until a verified capability is available.
 s.httpMillimeters=false;s.httpChannelIdle=false;
 printerFinalize(s);out=s;return true;
}
bool OctoPrintPrinterAdapter::list(PrinterTransport& t,const PrinterProfile&,const String& directory,String& json,String& error){
 if(directory=="0:/macros"){JsonDocument o;o["entries"].to<JsonArray>();o["pending"]=false;
  o["directory"]=directory;o["message"]="OctoPrint-Skripte werden hier nicht als Makros angeboten";
  json="";serializeJson(o,json);return true;}
 String sub;
 if(directory!="0:/gcodes"){
  if(!printerPath(directory.c_str(),"0:/gcodes/")){error="printer_path_invalid";return false;}
  sub=directory.substring(10);
 }
 PrinterJsonAllocator pool;JsonDocument d(&pool);
 const String endpoint=sub.length()?String("/api/files/local/")+encode(sub)+"?recursive=false":String("/api/files/local?recursive=false");
 if(!fetch(t,endpoint,d,error,32768))return false;
 JsonArrayConst children=sub.length()?d["children"].as<JsonArrayConst>():d["files"].as<JsonArrayConst>();
 if(children.isNull()){error="printer_invalid_model_or_limit";return false;}
 JsonDocument out;out["pending"]=false;out["directory"]=directory;auto a=out["entries"].to<JsonArray>();
 unsigned n=0;for(JsonObjectConst item:children){
  if(n>=32){out["truncated"]=true;break;}
  String name=item["name"]|"";
  const String type=item["type"]|"";
  if(name.length()!=strlen(name.c_str())||!printerPath(name.c_str(),"")||name.indexOf('/')>=0||(type!="folder"&&type!="machinecode"))continue;
  const String canonical=directory+"/"+name;
  if(!printerPath(canonical.c_str(),"0:/gcodes/"))continue;
  auto row=a.add<JsonObject>();row["name"]=name;row["path"]=canonical;row["directory"]=(type=="folder");row["size"]=item["size"]|0; ++n;
 }
 json="";serializeJson(out,json);if(json.length()>8192){error="printer_directory_limit";return false;}return true;
}
bool OctoPrintPrinterAdapter::command(PrinterTransport& t,const PrinterProfile& p,const String& kind,JsonVariantConst args,PrinterStatus& state,String& reply,String& error){
 if(!poll(t,p,state,error))return false;JsonDocument d;String path;
 if(kind=="pause"||kind=="resume"){
  if(kind=="pause"&&strcmp(state.state,"printing")){error="printer_not_printing";return false;}
  if(kind=="resume"&&strcmp(state.state,"paused")){error="printer_not_paused";return false;}
  path="/api/job";d["command"]="pause";d["action"]=kind;
 }else if(kind=="cancel"){
  if(!printerActive(state)){error="printer_not_printing";return false;}
  path="/api/job";d["command"]="cancel";
 }else if(kind=="start_file"){
  if(!printerIdle(state)){error="printer_not_idle";return false;}
  String file=args["path"]|args["name"]|"";
  if(file.startsWith("0:/gcodes/"))file.remove(0,10);
  else if(file.startsWith("/gcodes/"))file.remove(0,8);
  if(file.length()!=strlen(file.c_str())||!printerPath(file.c_str(),"")){error="printer_path_invalid";return false;}
  path="/api/files/local/"+encode(file);d["command"]="select";d["print"]=true;
 }else if(kind=="home"){
  if(!printerIdle(state)){error="printer_not_idle";return false;}
  const String axes=args["axes"]|"xyz";
  if(axes!="x"&&axes!="y"&&axes!="z"&&axes!="xy"&&axes!="xyz"){error="printer_argument_axes";return false;}
  path="/api/printer/printhead";d["command"]="home";auto list=d["axes"].to<JsonArray>();
  for(size_t i=0;i<axes.length();++i){char axis[2]={axes[i],0};list.add(axis);}
 }else if(kind=="select_tool"){
  const auto* tool=printerFindTool(state,args["tool"]|"");
  if(!printerIdle(state)||!tool){error="printer_tool_not_safe";return false;}
  path="/api/printer/tool";d["command"]="select";d["tool"]=tool->id;
 }else if(kind=="bed_mesh"||kind=="z_tilt"){
  const uint32_t flag=kind=="bed_mesh"?PrinterMesh:PrinterZTilt;
  if(!printerIdle(state)||!(state.controls&flag)){error="printer_calibration_not_safe";return false;}
  // Only explicitly configured Marlin routines. OctoPrint cannot report the
  // homed axes reliably, so this confirmed operation includes homing first.
  path="/api/printer/command";auto commands=d["commands"].to<JsonArray>();commands.add("G28");
  commands.add(kind=="z_tilt"?"G34":p.calibration=="marlin_ubl"?"G29 P1":"G29");
 }else if(kind=="set_speed_factor"){
  const double percent=args["percent"]|NAN;
  if(!std::isfinite(percent)||percent<50||percent>200||(!printerIdle(state)&&!printerActive(state))){error="printer_argument_speed";return false;}
  path="/api/printer/printhead";d["command"]="feedrate";d["factor"]=static_cast<int>(std::lround(percent));
 }else{
  // No unverified movement, heating, arbitrary G-code or macros over OctoPrint.
  error="printer_command_unsupported";return false;
 }
 String body;serializeJson(d,body);if(!send(t,path,body,error))return false;
 reply="OctoPrint hat den Auftrag angenommen; Ausführung wird über Status geprüft";return true;
}
}
