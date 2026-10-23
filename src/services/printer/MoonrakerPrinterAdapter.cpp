#include "MoonrakerPrinterAdapter.h"
#include "PrinterJson.h"
#include "PrinterComponents.h"
#include "PrinterThumbnailPolicy.h"
#include <algorithm>
#include <cmath>
namespace hp { namespace {
static bool failure(const LuaJobService::Result& r,String& error){
    if(r.ok)return false;
    error=(r.status==401||r.status==403)?"printer_authentication":
          r.status==404?"printer_moonraker_endpoint_missing":
          r.status==503?"printer_service_unavailable":
          r.error.length()?r.error:String("printer_http_")+r.status;
    return true;
}
static bool decode(const String& s,JsonDocument& doc,String& error){
    if(!printerDecodeModel(s,doc,error))return false;
    if(doc["error"].is<JsonObject>()) { error="printer_moonraker_rpc_error";return false; }
    return true;
}
static double number(JsonVariantConst v,double fallback=0){
    if(v.is<double>()||v.is<int>()||v.is<unsigned>())return v.as<double>();
    return fallback;
}
static void heater(JsonVariantConst obj,PrinterHeater& h,int idx,double max){
    if(obj.isNull())return;
    h.index=idx;h.current=printerNumber(obj["temperature"]);h.target=printerNumber(obj["target"]);
    h.max=max;h.limit=std::isfinite(max)&&max>0;h.fault=false;
}
static bool active(const PrinterStatus& s){return printerActive(s);}
static bool idle(const PrinterStatus& s){return printerIdle(s);}
static bool isGood(const LuaJobService::Result& r, String& error){
    if(failure(r,error))return false;
    PrinterJsonAllocator mem;JsonDocument doc(&mem);
    if(!decode(r.body,doc,error))return false;
    if(doc["result"].isNull()){error="printer_moonraker_result_missing";return false;}
    return true;
}
static String gcodeRequest(const String& script){
    JsonDocument doc;doc["script"]=script;String result;serializeJson(doc,result);return result;
}
}
String MoonrakerPrinterAdapter::encode(const String& s){return printerEncode(s);}
bool MoonrakerPrinterAdapter::discover(PrinterTransport& t,String& error){
    auto r=t.request("/printer/objects/list","",0,"",32768);if(failure(r,error))return false;
    PrinterJsonAllocator memory;JsonDocument doc(&memory);if(!decode(r.body,doc,error))return false;
    auto objects=doc["result"]["objects"].as<JsonArrayConst>();if(objects.isNull()){error="printer_moonraker_objects_missing";return false;}
    String query="/printer/objects/query?print_stats&virtual_sdcard&toolhead&gcode_move&configfile&idle_timeout&webhooks&heaters";size_t count=0;discoveryTruncated_=false;
    for(auto object:objects){if(!object.is<const char*>()||object.as<JsonString>().size()!=strlen(object.as<const char*>())){error="printer_moonraker_objects_invalid";return false;}const String name=object.as<String>();
        int index=0;if(!(name=="extruder"||printerIndexedName(name.c_str(),"extruder",index)||name=="heater_bed"||name.startsWith("heater_generic ")||name.startsWith("temperature_sensor ")||name.startsWith("temperature_fan ")||name.startsWith("temperature_host ")||name=="fan"||name.startsWith("fan_generic ")||name.startsWith("heater_fan ")||name.startsWith("controller_fan ")))continue;
        if(name.length()>=64||count>=40||query.length()+name.length()*3+1>4096){discoveryTruncated_=true;continue;}
        query+="&"+encode(name);++count;
    }
    query_=query;discoveredAt_=millis();return true;
}
bool MoonrakerPrinterAdapter::poll(PrinterTransport& t,const PrinterProfile&,PrinterStatus& out,String& error){
    if(!query_.length())if(!discover(t,error))return false;
    auto r=t.request(query_, "",0,"",65536);
    if(failure(r,error)){query_="";return false;}
    PrinterJsonAllocator mem;JsonDocument doc(&mem);
    if(!decode(r.body,doc,error))return false;
    JsonVariantConst model=doc["result"]["status"];
    if(!model.is<JsonObjectConst>()){error="printer_moonraker_status_missing";return false;}
    auto buffer=printerStatusBuffer();if(!buffer){error="printer_memory";return false;}auto& s=*buffer;s.truncated=discoveryTruncated_;
    if(model["webhooks"]["state"].is<const char*>()&&String(model["webhooks"]["state"]|"")!="ready"){query_="";error="printer_service_unavailable";return false;}
    const double currentTime=printerNumber(doc["result"]["eventtime"]);if(std::isfinite(currentTime)){if(eventTime_>=0&&currentTime<eventTime_)query_="";eventTime_=currentTime;}
    if(model["heaters"]["available_sensors"].is<JsonArrayConst>()){
        uint32_t hash=2166136261u;for(const char* key:{"available_sensors","available_heaters"})for(auto value:model["heaters"][key].as<JsonArrayConst>()){const char* name=value|"";for(const char* c=name;*c;++c)hash=(hash^uint8_t(*c))*16777619u;hash=(hash^0xffu)*16777619u;}
        if(inventoryHash_&&inventoryHash_!=hash)query_="";inventoryHash_=hash;
    }
    const String state=model["print_stats"]["state"]|"";
    if(state=="printing")strlcpy(s.state,"printing",sizeof(s.state));
    else if(state=="paused")strlcpy(s.state,"paused",sizeof(s.state));
    else if(state=="standby"||state=="complete"||state=="cancelled")strlcpy(s.state,"idle",sizeof(s.state));
    else if(state=="error")strlcpy(s.state,"error",sizeof(s.state));
    else{error="printer_moonraker_unknown_state";return false;}
    strlcpy(s.filename,model["print_stats"]["filename"]|"",sizeof(s.filename));
    s.duration=printerNumber(model["print_stats"]["print_duration"]);
    s.progress=printerNumber(model["virtual_sdcard"]["progress"])*100.0;
    s.speedFactor=printerNumber(model["gcode_move"]["speed_factor"])*100.0;s.flowFactor=printerNumber(model["gcode_move"]["extrude_factor"])*100;
    const String motionState=model["idle_timeout"]["state"]|"";
    s.httpMillimeters=true;s.httpChannelIdle=(motionState=="Ready"||motionState=="Idle");
    s.layer=model["print_stats"]["info"]["current_layer"]|0u;
    s.layers=model["print_stats"]["info"]["total_layer"]|0u;
    s.layerKnown=model["print_stats"]["info"]["current_layer"].is<uint32_t>();s.layersKnown=model["print_stats"]["info"]["total_layer"].is<uint32_t>();
    // Only trust limits supplied by the printer; no guessed travel or heater limits.
    JsonVariantConst cfg=model["configfile"]["settings"];
    s.controls=PrinterFlow|PrinterMotorsOff;
    s.knownControls=511;s.configurableControls=PrinterMesh|PrinterZTilt|PrinterGantry|PrinterScrews|PrinterMeshClear;
    if(!cfg.is<JsonObjectConst>())s.knownControls&=~s.configurableControls;
    if(cfg["bed_mesh"].is<JsonObjectConst>())s.controls|=PrinterMesh|PrinterMeshClear;
    if(cfg["z_tilt"].is<JsonObjectConst>())s.controls|=PrinterZTilt;
    if(cfg["quad_gantry_level"].is<JsonObjectConst>())s.controls|=PrinterGantry;
    if(cfg["screws_tilt_adjust"].is<JsonObjectConst>())s.controls|=PrinterScrews;
    heater(model["extruder"],s.nozzle,0,number(cfg["extruder"]["max_temp"]));
    // M104 targets the active extruder. Never apply extruder0 limits to another tool.
    const String activeExtruder=model["toolhead"]["extruder"]|"";
    s.canExtrude=model[activeExtruder]["can_extrude"].is<bool>()&&model[activeExtruder]["can_extrude"].as<bool>();
    if(activeExtruder!="extruder")s.nozzle.limit=false;
    heater(model["heater_bed"],s.bed,0,number(cfg["heater_bed"]["max_temp"]));
    s.bed.category=PrinterTemperatureKind::Bed;
    s.fanAvailability=PrinterAvailability::Absent;s.axisAvailability=PrinterAvailability::Available;s.toolAvailability=PrinterAvailability::Absent;s.extruderAvailability=PrinterAvailability::Absent;
    for(JsonPairConst item:model.as<JsonObjectConst>()){
        const String name=item.key().c_str();auto value=item.value();int extruderIndex=0;const bool extruder=name=="extruder"||printerIndexedName(name.c_str(),"extruder",extruderIndex);
        const bool heated=extruder||name=="heater_bed"||name.startsWith("heater_generic ");
        if(heated||name.startsWith("temperature_sensor ")||name.startsWith("temperature_host ")||name.startsWith("temperature_fan ")){
            const auto category=extruder?PrinterTemperatureKind::Hotend:name=="heater_bed"?PrinterTemperatureKind::Bed:name.startsWith("temperature_host ")?PrinterTemperatureKind::Host:PrinterTemperatureKind::Sensor;
            const double maximum=printerNumber(cfg[name]["max_temp"]);int idx=-1;bool controllable=false;
            if(heated){if(s.heaterCount==s.kHeaters)s.truncated=true;else {idx=s.heaterCount;auto& h=s.heaters[s.heaterCount++];heater(value,h,idx,maximum);strlcpy(h.id,name.c_str(),sizeof(h.id));strlcpy(h.name,name.c_str(),sizeof(h.name));h.category=category;String commandName=name;if(commandName.startsWith("heater_generic "))commandName=commandName.substring(15);h.mappingSafe=printerIdentity(commandName.c_str(),48);}}
            // s.valid is set below; physical capability is determined independently.
            if(idx>=0)controllable=s.heaters[idx].mappingSafe&&s.heaters[idx].limit&&std::isfinite(s.heaters[idx].current)&&!s.heaters[idx].fault;
            printerTemperature(s,name.c_str(),name.c_str(),category,printerNumber(value["temperature"]),heated||name.startsWith("temperature_fan ")?printerNumber(value["target"]):NAN,idx,controllable);
            if(extruder){if(s.toolCount<s.kTools){auto& tool=s.tools[s.toolCount++];tool.number=extruderIndex;strlcpy(tool.id,name.c_str(),sizeof(tool.id));strlcpy(tool.name,name.c_str(),sizeof(tool.name));tool.active=name==activeExtruder;tool.activeKnown=activeExtruder.length()>0;if(idx>=0){tool.heaterCount=1;tool.heaters[0]=idx;}}
                else s.truncated=true;if(s.extruderCount<s.kExtruders){auto& e=s.extruders[s.extruderCount++];e.index=extruderIndex;strlcpy(e.id,name.c_str(),sizeof(e.id));strlcpy(e.name,name.c_str(),sizeof(e.name));}else s.truncated=true;}
        }
        if(name=="fan"||name.startsWith("fan_generic ")||name.startsWith("heater_fan ")||name.startsWith("controller_fan ")||name.startsWith("temperature_fan ")){
            if(s.fanCount==16){s.truncated=true;continue;}auto& f=s.fans[s.fanCount];f.index=name=="fan"?0:int(s.fanCount)+1;++s.fanCount;strlcpy(f.id,name.c_str(),sizeof(f.id));strlcpy(f.name,name.c_str(),sizeof(f.name));f.value=printerNumber(value["speed"]);f.rpm=printerNumber(value["rpm"]);f.automatic=name!="fan"&&(!name.startsWith("fan_generic ")||!printerIdentity(name.substring(12).c_str(),48));
        }
    }
    if(s.fanCount)s.fanAvailability=PrinterAvailability::Available;if(s.toolCount)s.toolAvailability=PrinterAvailability::Available;if(s.extruderCount)s.extruderAvailability=PrinterAvailability::Available;
    if(s.toolCount)s.controls|=PrinterToolSelect|PrinterExtrude;
    bool safeActiveHeater=false;
    for(size_t i=0;i<s.heaterCount;++i)if(activeExtruder==s.heaters[i].id){const auto& h=s.heaters[i];
        safeActiveHeater=h.mappingSafe&&h.limit&&!h.fault&&std::isfinite(h.current)&&h.current<=h.max;}
    s.canExtrude=s.canExtrude&&safeActiveHeater;
    const String homed=model["toolhead"]["homed_axes"]|"";
    JsonVariantConst pos=model["toolhead"]["position"];
    JsonVariantConst amin=model["toolhead"]["axis_minimum"];
    JsonVariantConst amax=model["toolhead"]["axis_maximum"];
    for(int i=0;i<3;++i){auto& a=s.axes[s.axisCount++];a.letter="XYZ"[i];
        a.homed=homed.indexOf("xyz"[i])>=0;a.homedKnown=model["toolhead"]["homed_axes"].is<const char*>();
        a.position=printerNumber(pos[i]);a.machinePosition=a.position;a.min=printerNumber(amin[i]);a.max=printerNumber(amax[i]);
        a.maxFeed=number(cfg["printer"]["max_velocity"])*60.0;
        if(i==2)a.maxFeed=std::min(a.maxFeed,number(cfg["printer"]["max_z_velocity"],a.maxFeed/60.0)*60.0);
        a.limits=pos[i].is<double>()&&amin[i].is<double>()&&amax[i].is<double>()&&a.max>a.min;}
    s.valid=true;printerFinalize(s);out=s;return true;
}
bool MoonrakerPrinterAdapter::list(PrinterTransport& t,const PrinterProfile&,const String& dir,String& json,String& error){
    JsonDocument out;auto entries=out["entries"].to<JsonArray>();out["directory"]=dir;out["pending"]=false;
    const bool macros=(dir=="0:/macros");
    String sub;
    if(!macros&&dir!="0:/gcodes"){
        if(!printerPath(dir.c_str(),"0:/gcodes/")){error="printer_path_invalid";return false;}
        sub=dir.substring(10);
    }
    LuaJobService::Result r;
    if(macros)r=t.request("/printer/objects/query?configfile","",0,"",65536);
    else r=t.request(String("/server/files/directory?path=")+encode(sub.length()?String("gcodes/")+sub:String("gcodes"))+"&extended=false","",0,"",32768);
    if(failure(r,error))return false;
    PrinterJsonAllocator mem;JsonDocument doc(&mem);
    if(!decode(r.body,doc,error))return false;
    size_t count=0;
    if(macros){
        JsonObjectConst cfg=doc["result"]["status"]["configfile"]["settings"].as<JsonObjectConst>();
        if(cfg.isNull()){error="printer_invalid_model_or_limit";return false;}
        for(JsonPairConst item:cfg){String name=item.key().c_str();if(!name.startsWith("gcode_macro "))continue;
            const String macro=name.substring(12);
            if(macro.length()!=strlen(macro.c_str())||!printerIdentity(macro.c_str(),96)||macro[0]=='_')continue;
            if(count++>=32){out["truncated"]=true;break;}
            auto entry=entries.add<JsonObject>();
            entry["name"]=macro;entry["path"]=macro;entry["directory"]=false;}
    }else{
        const auto result=doc["result"];
        if(!result["dirs"].is<JsonArrayConst>()||!result["files"].is<JsonArrayConst>()){
            error="printer_invalid_model_or_limit";return false;}
        for(const char* key:{"dirs","files"}){
            const bool folder=strcmp(key,"dirs")==0;
            for(JsonObjectConst item:result[key].as<JsonArrayConst>()){
                const String name=folder?String(item["dirname"]|""):String(item["filename"]|"");
                if(name.length()!=strlen(name.c_str())||!printerPath(name.c_str(),"")||name.indexOf('/')>=0)continue;
                const String canonical=dir+"/"+name;
                if(!printerPath(canonical.c_str(),"0:/gcodes/"))continue;
                if(count>=32){out["truncated"]=true;break;}++count;
                auto entry=entries.add<JsonObject>();entry["name"]=name;
                entry["path"]=canonical;entry["directory"]=folder;entry["size"]=item["size"]|0u;
            }
        }
    }
    json="";serializeJson(out,json);if(json.length()>8192){error="printer_directory_limit";return false;}
    return true;
}
bool MoonrakerPrinterAdapter::command(PrinterTransport& t,const PrinterProfile& p,const String& kind,JsonVariantConst a,PrinterStatus& s,String& reply,String& error){
    // Shared safety helpers require a fresh status directly from the worker.
    if(!poll(t,p,s,error))return false;
    String path,script,body;uint8_t method=1;
    if(kind=="pause"){if(!active(s)||strcmp(s.state,"printing")){error="printer_not_printing";return false;}path="/printer/print/pause";}
    else if(kind=="resume"){if(strcmp(s.state,"paused")){error="printer_not_paused";return false;}path="/printer/print/resume";}
    else if(kind=="cancel"){if(!active(s)){error="printer_not_printing";return false;}path="/printer/print/cancel";}
    else if(kind=="home"){
        if(!idle(s)||!s.httpChannelIdle){error="printer_not_idle";return false;}
        String axes=a["axes"]|"xyz";if(axes!="x"&&axes!="y"&&axes!="z"&&axes!="xy"&&axes!="xyz"){error="printer_argument_axes";return false;}
        script="G28";for(size_t i=0;i<axes.length();++i)script+=String(" ")+char(toupper(axes[i]));
    }else if(kind=="move"){
        const String axis=a["axis"]|"";const double distance=a["distance"]|NAN;
        if(axis.length()!=1||!a["relative"].as<bool>()||!printerJogSafe(s,toupper(axis[0]),distance)){
            error="printer_move_not_safe";return false;}
        double feed=600;
        for(size_t i=0;i<s.axisCount;++i)if(s.axes[i].letter==toupper(axis[0]))feed=std::min(feed,s.axes[i].maxFeed);
        script="SAVE_GCODE_STATE NAME=HP_JOG\nG91\nG1 ";script+=char(toupper(axis[0]));
        script+=String(distance,3)+" F"+String(feed,1)+"\nRESTORE_GCODE_STATE NAME=HP_JOG";
    }else if(kind=="bed_mesh"||kind=="z_tilt"||kind=="gantry_level"||kind=="screws_tilt"||kind=="mesh_clear"){
        const uint32_t flag=kind=="bed_mesh"?PrinterMesh:kind=="z_tilt"?PrinterZTilt:kind=="gantry_level"?PrinterGantry:kind=="screws_tilt"?PrinterScrews:PrinterMeshClear;
        if(!(s.controls&flag)||!printerCalibrationSafe(s)){error="printer_calibration_not_safe";return false;}
        script=kind=="bed_mesh"?"BED_MESH_CALIBRATE":kind=="z_tilt"?"Z_TILT_ADJUST":kind=="gantry_level"?"QUAD_GANTRY_LEVEL":kind=="screws_tilt"?"SCREWS_TILT_CALCULATE":"BED_MESH_CLEAR";
    }else if(kind=="select_tool"||kind=="extrude"||kind=="set_flow_factor"){
        const auto* tool=printerFindTool(s,a["tool"]|"");
        if(!tool||!printerIdentity(tool->id,48)){error="printer_tool_unavailable";return false;}
        if(kind=="select_tool"){
            if(!printerCalibrationSafe(s)){error="printer_tool_not_safe";return false;}
            script="ACTIVATE_EXTRUDER EXTRUDER="+String(tool->id);
        }else if(!tool->activeKnown||!tool->active){error="printer_tool_not_active";return false;}
        else if(kind=="extrude"){
            const double distance=a["distance"]|NAN,feed=a["feed"]|NAN;
            if(!printerExtrusionSafe(s,distance,feed)){error="printer_extrude_not_safe";return false;}
            script="SAVE_GCODE_STATE NAME=HP_EXTRUDE\nM83\nG1 E"+String(distance,3)+" F"+String(feed,1)+"\nRESTORE_GCODE_STATE NAME=HP_EXTRUDE";
        }else {const double pct=a["percent"]|NAN;
            if((!idle(s)&&!active(s))||!std::isfinite(pct)||pct<10||pct>200){error="printer_argument_flow";return false;}
            script="M221 S"+String(pct,1);}
    }else if(kind=="motors_off"){
        if(!idle(s)||!s.httpChannelIdle){error="printer_not_idle";return false;}script="M18";
    }else if(kind=="set_temperature"){
        String which=a["heater"]|"";double target=a["target"]|NAN;const auto* h=printerFindHeater(s,which.c_str());bool bed=h&&h->category==PrinterTemperatureKind::Bed;
        if(!h||!printerHeatSafe(s,*h,target,bed)||which=="nozzle"&&!s.nozzle.limit){
            error="printer_temperature_not_safe";return false;}
        String name=which=="nozzle"?"extruder":which=="bed"?"heater_bed":String(h->id);
        if(name.startsWith("temperature_fan ")){name=name.substring(16);if(!printerIdentity(name.c_str(),48)){error="printer_temperature_not_safe";return false;}script="SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN="+name+" TARGET="+String(target,1);}
        else {if(name.startsWith("heater_generic "))name=name.substring(15);if(!printerIdentity(name.c_str(),48)){error="printer_temperature_not_safe";return false;}script="SET_HEATER_TEMPERATURE HEATER="+name+" TARGET="+String(target,1);}
    }else if(kind=="set_speed_factor"){
        double pct=a["percent"]|NAN;if(!std::isfinite(pct)||pct<10||pct>300||(!idle(s)&&!active(s))){error="printer_argument_speed";return false;}
        script="M220 S"+String(pct,1);
    }else if(kind=="set_fan"){
        double pct=a["percent"]|NAN;int index=a["fan"]|0;
        const String requested=a["fan_id"]|"";const PrinterFan* fan=nullptr;for(size_t i=0;i<s.fanCount;++i)if(requested.length()?requested==s.fans[i].id:s.fans[i].index==index)fan=&s.fans[i];
        if(!std::isfinite(pct)||pct<0||pct>100||!fan||fan->automatic||(!idle(s)&&!active(s))){error="printer_argument_fan";return false;}
        if(!strcmp(fan->id,"fan"))script="M106 S"+String((int)round(pct*2.55));
        else {String name=String(fan->id).substring(12);if(!printerIdentity(name.c_str(),48)){error="printer_fan_protected";return false;}script="SET_FAN_SPEED FAN="+name+" SPEED="+String(pct/100,3);}
    }else if(kind=="start_file"){
        if(!idle(s)){error="printer_not_idle";return false;}
        String file=a["path"]|a["name"]|"";
        if(file.startsWith("0:/gcodes/"))file.remove(0,10);
        if(file.length()!=strlen(file.c_str())||!printerPath(file.c_str(),"")||file.indexOf(':')>=0){error="printer_path_invalid";return false;}
        path="/printer/print/start?filename="+encode(file);
    }else if(kind=="run_macro"){
        if(!idle(s)||!s.httpChannelIdle){error="printer_not_idle";return false;}
        const String name=a["name"]|a["path"]|"";
        if(name.length()!=strlen(name.c_str())||!printerIdentity(name.c_str(),96)||name[0]=='_'){error="printer_path_invalid";return false;}
        const auto configured=t.request("/printer/objects/query?configfile","",0,"",65536);
        if(failure(configured,error))return false;
        PrinterJsonAllocator memory;JsonDocument config(&memory);
        if(!decode(configured.body,config,error))return false;
        const String section=String("gcode_macro ")+name;
        if(!config["result"]["status"]["configfile"]["settings"][section].is<JsonObjectConst>()){
            error="printer_macro_not_configured";return false;}
        script=name;
    }else if(kind=="gcode"){
        script=a["code"]|"";script.trim();script.toUpperCase();
        if(script!="M115"&&script!="M119"&&script!="M105"&&script!="M114"){
            error="printer_gcode_use_typed_command";return false;}
    }else{error="printer_command_unsupported";return false;}
    if(t.stopped()){error="cancelled";return false;}
    if(script.length()){path="/printer/gcode/script";body=gcodeRequest(script);}
    const auto r=t.request(path,"",method,body,4096);
    if(!isGood(r,error)){if(!r.status)error="printer_command_outcome_unknown";return false;}
    reply="Moonraker hat den Befehl angenommen";return true;
}
bool MoonrakerPrinterAdapter::thumbnail(PrinterTransport& t,const PrinterProfile&,const String& path,String& bytes,String& error){
    if(!printerPath(path.c_str(),"0:/gcodes/")){error="printer_path_invalid";return false;}
    const String relative=path.substring(10);
    auto r=t.request("/server/files/metadata?filename="+encode(relative),"",0,"",16384);
    if(failure(r,error))return false;JsonDocument doc;if(!decode(r.body,doc,error))return false;
    String bestPath;size_t best=0;PrinterThumbnailChoice choice{t.previewWidth,t.previewHeight};
    for(auto item:doc["result"]["thumbnails"].as<JsonArrayConst>()){
        const unsigned w=item["width"]|0u,h=item["height"]|0u;const size_t size=item["size"]|0u;
        const String thumbnail=item["relative_path"]|"";
        const String folder=relative.lastIndexOf('/')>=0?relative.substring(0,relative.lastIndexOf('/')+1):String();
        const String full="0:/gcodes/"+folder+thumbnail;
        if(size&&size<=65536&&printerPath(full.c_str(),"0:/gcodes/")&&(thumbnail.endsWith(".png")||thumbnail.endsWith(".jpg")||thumbnail.endsWith(".jpeg"))&&choice.consider(w,h)){best=w*h;bestPath=full.substring(10);}
    }
    if(!best){error="printer_thumbnail_missing_or_limit";return false;}
    auto image=t.request("/server/files/gcodes/"+encode(bestPath),"",0,"",65536);
    if(failure(image,error))return false;bytes=std::move(image.body);return true;
}
}
