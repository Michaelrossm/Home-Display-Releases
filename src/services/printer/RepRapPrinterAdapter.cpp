#include "RepRapPrinterAdapter.h"
#include "PrinterJson.h"
#include "PrinterComponents.h"
#include "PrinterThumbnailPolicy.h"
#include <algorithm>
namespace hp {namespace {
bool parse(const String& text,JsonDocument& doc,String& error){
    return printerDecodeModel(text,doc,error);
}
bool statusError(const LuaJobService::Result& r,String& error){
    if(r.ok)return false;
    error=r.status==401||r.status==403?"printer_authentication":r.status==503?"printer_service_unavailable":r.status==502?"printer_dsf_incompatible":
        r.error.length()?r.error:String("printer_http_")+r.status;return true;
}
void heater(JsonVariantConst model,int index,int tool,PrinterHeater& out){
    if(index<0||index>=32)return;auto h=model["heat"]["heaters"][index];if(h.isNull())return;
    out.index=index;out.tool=tool;out.sensor=h["sensor"]|-1;out.current=printerNumber(h["current"]);out.target=printerNumber(h["active"]);
    const String state=h["state"]|"";
    out.fault=state!="active"&&state!="standby"&&state!="off";
    if(!h["max"].isNull()){out.max=h["max"].as<double>();out.limit=std::isfinite(out.max)&&out.max>0;}
    for(auto monitor:h["monitors"].as<JsonArrayConst>()){
        const String condition=monitor["condition"]|"";const double limit=monitor["limit"]|0.0;
        if(condition=="tooHigh"&&std::isfinite(limit)&&limit>0&&(!out.limit||limit<out.max)){out.max=limit;out.limit=true;}
    }
}
bool mappedHeater(JsonVariantConst heat,const char* legacy,const char* mapping,int index,int& group,bool& single){
    auto groups=heat[mapping].as<JsonArrayConst>();int at=0;
    for(auto values:groups){for(auto value:values.as<JsonArrayConst>())if(value.is<int>()&&value.as<int>()==index){group=at;single=values.size()==1;return true;}++at;}
    at=0;for(auto value:heat[legacy].as<JsonArrayConst>()){if(value.is<int>()&&value.as<int>()==index){group=at;single=true;return true;}++at;}return false;
}
}
String RepRapPrinterAdapter::encode(const String& s){return printerEncode(s);}
bool RepRapPrinterAdapter::connect(PrinterTransport& http,const PrinterProfile& p,String& error){
    if(session_.length()&&int32_t(expires_-millis())>0)return true;
    session_="";const String password=http.password();
    if(!password.length()){error="printer_secret_missing";return false;}
    sbc_=p.mode=="sbc";
    const String authQuery=String("password")+"="+encode(password);
    auto r=http.request(sbc_?String("/machine/connect?")+authQuery:String("/rr_connect?sessionKey=yes&")+authQuery,"");
    // Probe another API only for a missing endpoint, never for an auth failure.
    if(!sbc_&&p.mode!="standalone"&&(r.status==404||r.status==405)){
        sbc_=true;r=http.request("/machine/connect?"+authQuery,"");
    }
    if(statusError(r,error))return false;
    PrinterJsonAllocator allocator;JsonDocument doc(&allocator);if(!parse(r.body,doc,error))return false;
    if(!sbc_&&(doc["err"]|1)!=0){error=(doc["err"]|1)==1?"printer_authentication":"printer_sessions_full";return false;}
    if(doc["sessionKey"].is<const char*>())session_=doc["sessionKey"].as<String>();
    else if(doc["sessionKey"].is<uint32_t>())session_=String(doc["sessionKey"].as<uint32_t>());
    if(!session_.length()||session_.length()>128||session_.indexOf('\r')>=0||session_.indexOf('\n')>=0){session_="";error="printer_invalid_session";return false;}
    sessionTimeout_=sbc_?60000:std::max<uint32_t>(1000,std::min<uint32_t>(60000,doc["sessionTimeout"]|8000u));
    expires_=millis()+sessionTimeout_-500;return true;
}
LuaJobService::Result RepRapPrinterAdapter::fetch(PrinterTransport& t,const String& path,uint8_t method,const String& body,size_t limit){
    auto r=t.request(path,session_,method,body,limit);
    // A backed-up HTTP reply buffer may produce 503. Drain/retry reads only.
    // Never retry rr_gcode or machine/code: the first command may have executed.
    if(!sbc_&&method==0&&path.startsWith("/rr_model")&&r.status==503&&!t.stopped()){
        auto drained=t.request("/rr_reply",session_,0,"",2048);
        if(drained.ok){reply_=drained.body.substring(0,512);r=t.request(path,session_,method,body,limit);}
    }
    if(r.status==401||r.status==403)session_="";
    else if(r.ok)expires_=millis()+sessionTimeout_-500;
    return r;
}
bool RepRapPrinterAdapter::pollConsole(PrinterTransport& t,const PrinterProfile& p,String& error){
    if(!connect(t,p,error))return false;
    if(sbc_){error="printer_console_stream_unsupported";return false;}
    auto sequenceResponse=fetch(t,"/rr_model?key=seqs.reply&flags=d5",0,"",256);
    if(statusError(sequenceResponse,error))return false;
    JsonDocument doc;
    if(deserializeJson(doc,sequenceResponse.body)||!doc["result"].is<uint32_t>()){
        error="printer_invalid_reply_sequence";return false;
    }
    const auto sequence=doc["result"].as<uint32_t>();
    if(replyKnown_&&sequence==replySequence_)return true;
    if(t.stopped()){error="cancelled_or_offline";return false;}
    auto response=fetch(t,"/rr_reply",0,"",2048);
    if(statusError(response,error))return false;
    if(response.body.length()){
        if(reply_.length())reply_+='\n';
        reply_+=response.body.substring(0,512);
        replySequence_=sequence;replyKnown_=true;
    }
    return true;
}
bool RepRapPrinterAdapter::normalize(JsonVariantConst m,PrinterStatus& out,String& error){
    auto buffer=printerStatusBuffer();if(!buffer){error="printer_memory";return false;}auto& s=*buffer;const String state=m["state"]["status"]|"";
    if(state.length()==0||state.length()>=sizeof(s.state)){error="printer_model_state_missing";return false;}
    strlcpy(s.state,state.c_str(),sizeof(s.state));
    for(auto input:m["inputs"].as<JsonArrayConst>())if(String(input["name"]|"")=="HTTP"){
        s.httpMillimeters=String(input["distanceUnit"]|"")=="mm";
        const String channelState=input["state"]|"";
        s.httpChannelIdle=(channelState=="idle"||channelState=="unused")&&!(input["inMacro"]|true)&&!(input["inverseTimeMode"]|true);
    }
    auto axes=m["move"]["axes"].as<JsonArrayConst>();
    s.axisAvailability=axes.isNull()?PrinterAvailability::Unavailable:axes.size()?PrinterAvailability::Available:PrinterAvailability::Absent;
    for(auto a:axes){if(s.axisCount==12){s.truncated=true;break;}auto& dst=s.axes[s.axisCount++];const char* letter=a["letter"]|"";
        dst.letter=letter[0];dst.homed=a["homed"]|false;dst.homedKnown=a["homed"].is<bool>();dst.position=printerNumber(a["userPosition"]);dst.machinePosition=printerNumber(a["machinePosition"]);dst.min=printerNumber(a["min"]);dst.max=printerNumber(a["max"]);
        dst.limits=a["min"].is<double>()&&a["max"].is<double>()&&std::isfinite(dst.min)&&std::isfinite(dst.max)&&dst.min<dst.max&&a["machinePosition"].is<double>();
        dst.maxFeed=a["speed"]|0.0;}
    const int selected=m["state"]["currentTool"]|-1;auto tools=m["tools"].as<JsonArrayConst>();int tool=-1,index=-1;
    for(auto t:tools)if((t["number"]|-1)==selected||(selected<0&&tools.size()==1)){tool=t["number"]|-1;
        if(t["heaters"].as<JsonArrayConst>().size()==1)index=t["heaters"][0]|-1;break;}
    heater(m,index,tool,s.nozzle);const auto bedMapping=m["heat"]["bedHeaterMapping"][0][0];const int bedIndex=bedMapping.is<int>()?bedMapping.as<int>():m["heat"]["bedHeaters"][0]|-1;heater(m,bedIndex,-1,s.bed);s.bed.category=PrinterTemperatureKind::Bed;
    // Sensors are canonical: heater/tool/bed references annotate the same entry.
    auto analog=m["sensors"]["analog"].as<JsonArrayConst>();int sensorIndex=0;
    s.temperatureAvailability=analog.isNull()&&m["heat"]["heaters"].isNull()?PrinterAvailability::Unavailable:PrinterAvailability::Absent;
    for(auto sensor:analog){const String type=sensor["type"]|"";const bool temperature=type.indexOf("humidity")<0&&type.indexOf("pressure")<0&&type.indexOf("gas")<0&&type!="current"&&type!="currentloop"&&!type.startsWith("ads131");if(!sensor.isNull()&&temperature){char id[64];snprintf(id,sizeof(id),"analog_%d",sensorIndex);const auto v=printerNumber(sensor["lastReading"]);
        auto* t=printerTemperature(s,id,sensor["name"]|id,PrinterTemperatureKind::Sensor,v);
        // Error readings are retained as unavailable, never as a believable zero.
        if(t&&(sensor["state"].is<const char*>()&&String(sensor["state"]|"")!="ok"))t->current=NAN;
    }++sensorIndex;}
    sensorIndex=0;for(auto sensor:m["sensors"]["digital"].as<JsonArrayConst>()){if(sensor["temperature"].is<double>()){char id[64];snprintf(id,sizeof(id),"digital_%d",sensorIndex);printerTemperature(s,id,sensor["name"]|id,PrinterTemperatureKind::Sensor,printerNumber(sensor["temperature"]));}++sensorIndex;}
    int heaterIndex=0;for(auto raw:m["heat"]["heaters"].as<JsonArrayConst>()){
        const int at=heaterIndex++;if(raw.isNull())continue;if(s.heaterCount==s.kHeaters){s.truncated=true;break;}auto& h=s.heaters[s.heaterCount++];heater(m,at,-1,h);
        snprintf(h.id,sizeof(h.id),"heater_%d",at);strlcpy(h.name,raw["name"]|h.id,sizeof(h.name));h.category=PrinterTemperatureKind::Hotend;
        h.mappingSafe=false;bool single=false;
        if(mappedHeater(m["heat"],"bedHeaters","bedHeaterMapping",at,h.group,single)){h.category=PrinterTemperatureKind::Bed;h.mappingSafe=single;}
        else if(mappedHeater(m["heat"],"chamberHeaters","chamberHeaterMapping",at,h.group,single)){h.category=PrinterTemperatureKind::Chamber;h.mappingSafe=single;}
        else for(auto v:tools)for(auto ref:v["heaters"].as<JsonArrayConst>())if(ref.is<int>()&&ref.as<int>()==at){h.tool=v["number"]|-1;h.mappingSafe=v["heaters"].size()==1;}
        char sensorId[64];snprintf(sensorId,sizeof(sensorId),h.sensor>=0?"analog_%d":"heater_sensor_%d",h.sensor>=0?h.sensor:at);
        PrinterTemperature* found=nullptr;for(size_t i=0;i<s.temperatureCount;++i)if(!strcmp(s.temperatures[i].id,sensorId))found=&s.temperatures[i];
        if(found){h.current=found->current;if(!std::isfinite(h.current))h.fault=true;found->category=h.category;
            if(found->heater>=0&&found->heater!=at||found->shared){found->shared=true;found->heater=-1;found->target=NAN;found->controllable=false;}
            else {found->target=h.target;found->heater=at;found->controllable=h.mappingSafe&&h.limit&&!h.fault&&std::isfinite(h.current);}}
        else printerTemperature(s,sensorId,h.name,h.category,h.current,h.target,at,h.mappingSafe&&h.limit&&!h.fault&&std::isfinite(h.current));
        if(h.index==s.nozzle.index)s.nozzle=h;if(h.index==s.bed.index)s.bed=h;
    }
    if(s.temperatureCount)s.temperatureAvailability=PrinterAvailability::Available;
    s.toolAvailability=tools.isNull()?PrinterAvailability::Unavailable:tools.size()?PrinterAvailability::Available:PrinterAvailability::Absent;
    for(auto raw:tools){if(s.toolCount==s.kTools){s.truncated=true;break;}auto& item=s.tools[s.toolCount++];item.number=raw["number"]|-1;snprintf(item.id,sizeof(item.id),"tool_%d",item.number);strlcpy(item.name,raw["name"]|item.id,sizeof(item.name));item.active=item.number==selected;item.activeKnown=m["state"]["currentTool"].is<int>();
        for(auto v:raw["heaters"].as<JsonArrayConst>()){if(item.heaterCount==8){s.truncated=true;break;}item.heaters[item.heaterCount++]=v.as<int>();}
        for(auto v:raw["extruders"].as<JsonArrayConst>()){if(item.extruderCount==8){s.truncated=true;break;}item.extruders[item.extruderCount++]=v.as<int>();}}
    auto extruders=m["move"]["extruders"].as<JsonArrayConst>();s.extruderAvailability=extruders.isNull()?PrinterAvailability::Unavailable:extruders.size()?PrinterAvailability::Available:PrinterAvailability::Absent;
    for(auto raw:extruders){if(s.extruderCount==s.kExtruders){s.truncated=true;break;}auto& item=s.extruders[s.extruderCount];item.index=s.extruderCount++;snprintf(item.id,sizeof(item.id),"extruder_%d",item.index);strlcpy(item.name,item.id,sizeof(item.name));item.position=printerNumber(raw["position"]);item.flowFactor=printerNumber(raw["factor"])*100;}
    auto fans=m["fans"].as<JsonArrayConst>();s.fanAvailability=fans.isNull()?PrinterAvailability::Unavailable:fans.size()?PrinterAvailability::Available:PrinterAvailability::Absent;
    int fanIndex=0;for(auto f:fans){if(!f.isNull()){if(s.fanCount==16){s.truncated=true;break;}auto& d=s.fans[s.fanCount++];d.index=fanIndex;d.value=printerNumber(f["actualValue"]);d.rpm=printerNumber(f["rpm"]);snprintf(d.id,sizeof(d.id),"fan_%d",fanIndex);strlcpy(d.name,f["name"]|d.id,sizeof(d.name));
        d.automatic=!f["thermostatic"]["sensors"].is<JsonArrayConst>()||f["thermostatic"]["sensors"].as<JsonArrayConst>().size()!=0;}++fanIndex;}
    auto job=m["job"];strlcpy(s.filename,job["file"]["fileName"]|job["file"]["name"]|"",sizeof(s.filename));
    const double size=job["file"]["size"]|0.0,position=job["filePosition"]|0.0;
    s.progress=size>0&&job["filePosition"].is<double>()?std::max(0.0,std::min(100.0,position*100.0/size)):NAN;
    s.duration=printerNumber(job["duration"]);s.remaining=printerNumber(job["timesLeft"]["file"]);s.layer=job["layer"]|0u;s.layers=job["file"]["numLayers"]|0u;s.layerKnown=job["layer"].is<uint32_t>();s.layersKnown=job["file"]["numLayers"].is<uint32_t>();
    strlcpy(s.printerName,m["network"]["name"]|"",sizeof(s.printerName));strlcpy(s.firmware,m["boards"][0]["firmwareVersion"]|"",sizeof(s.firmware));
    s.controls=PrinterFlow|PrinterMotorsOff|PrinterMeshClear;
    s.knownControls=511;s.configurableControls=PrinterMesh|PrinterZTilt;
    if(m["sensors"]["probes"].isNull()||m["move"]["compensation"].isNull())s.knownControls&=~PrinterMesh;
    if(s.toolCount)s.controls|=PrinterToolSelect;
    if(s.extruderCount)s.controls|=PrinterExtrude;
    const auto grid=m["move"]["compensation"]["probeGrid"];
    bool gridSafe=grid["axes"].size()==2;
    for(int i=0;i<2;++i)gridSafe=gridSafe&&grid["maxs"][i].is<double>()&&grid["mins"][i].is<double>()&&
        printerNumber(grid["maxs"][i])>printerNumber(grid["mins"][i])&&printerNumber(grid["spacings"][i])>0;
    if(gridSafe&&(m["sensors"]["probes"][0]["type"]|0)>0)s.controls|=PrinterMesh;
    s.minExtrudeTemperature=printerNumber(m["heat"]["coldExtrudeTemperature"]);
    for(size_t i=0;i<s.toolCount;++i)if(s.tools[i].activeKnown&&s.tools[i].active&&s.tools[i].heaterCount==1&&s.tools[i].extruderCount==1)
        for(size_t j=0;j<s.heaterCount;++j)if(s.heaters[j].index==s.tools[i].heaters[0]){
            const auto& h=s.heaters[j];s.canExtrude=h.mappingSafe&&h.limit&&!h.fault&&std::isfinite(h.current)&&h.current<=h.max&&
                s.minExtrudeTemperature>0&&h.current>=s.minExtrudeTemperature;}
    s.speedFactor=printerNumber(m["move"]["speedFactor"])*100.0;s.valid=true;printerFinalize(s);out=s;return true;
}
bool RepRapPrinterAdapter::poll(PrinterTransport& t,const PrinterProfile& p,PrinterStatus& s,String& error){
    if(!connect(t,p,error))return false;
    const bool refreshMetadata=!metadataKnown_||!s.valid||uint32_t(millis()-metadataAt_)>=30000;
    const String previousName=s.printerName,previousFirmware=s.firmware;
    // RRF standalone: never request the unbounded complete object model.
    // Some Duet boards reject /rr_model?key=&flags=d5 even though individual
    // branches are available. Filter each response before merging into PSRAM.
    // SBC/DSF retains its existing /machine/model protocol.
    JsonDocument filter;
    JsonObject f=sbc_?filter.to<JsonObject>():filter["result"].to<JsonObject>();
    f["state"]["status"]=true;f["state"]["currentTool"]=true;
    f["seqs"]["reply"]=true;
    for(const char* key:{"name","distanceUnit","state","inMacro","inverseTimeMode"})f["inputs"][0][key]=true;
    for(const char* key:{"letter","homed","userPosition","machinePosition","min","max","speed"})f["move"]["axes"][0][key]=true;
    f["move"]["speedFactor"]=true;f["move"]["extruders"]=true;
    f["move"]["compensation"]["probeGrid"]=true;
    f["heat"]["coldExtrudeTemperature"]=true;
    f["sensors"]["probes"][0]["type"]=true;
    f["heat"]["bedHeaters"]=true;f["heat"]["chamberHeaters"]=true;
    f["heat"]["bedHeaterMapping"]=true;f["heat"]["chamberHeaterMapping"]=true;
    f["sensors"]["analog"]=true;f["sensors"]["digital"]=true;
    f["network"]["name"]=true;f["boards"][0]["firmwareVersion"]=true;
    for(const char* key:{"current","active","state","max","monitors","sensor","name"})f["heat"]["heaters"][0][key]=true;
    for(const char* key:{"number","heaters","extruders","name"})f["tools"][0][key]=true;
    f["fans"][0]["actualValue"]=true;f["fans"][0]["thermostatic"]["sensors"]=true;
    f["fans"][0]["name"]=true;f["fans"][0]["rpm"]=true;
    for(const char* key:{"fileName","name","size","numLayers"})f["job"]["file"][key]=true;
    for(const char* key:{"filePosition","duration","layer","timesLeft"})f["job"][key]=true;
    PrinterJsonAllocator allocator;JsonDocument doc(&allocator);
    if(sbc_){
        auto r=fetch(t,"/machine/model",0,"",65536);
        if(statusError(r,error))return false;
        if(deserializeJson(doc,r.body,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16))){
            error="printer_invalid_model_or_limit";return false;
        }
    }else{
        // Required branches are queried individually. Optional branches may
        // be missing on older firmware and must not blank the whole display.
        struct Branch {const char* name; bool required; size_t limit;};
        static constexpr Branch branches[]={
            {"state",true,8192},{"heat",true,16384},{"sensors",true,16384},
            {"move",true,16384},{"tools",true,8192},{"fans",false,8192},
            {"job",false,8192},{"inputs",false,8192},{"network",false,4096},
            {"boards",false,8192},{"seqs",false,4096}
        };
        for(const auto& branch:branches){
            // Identity/version are not live telemetry. Retain them between
            // bounded refreshes; never cache motion/heater safety information.
            if(!refreshMetadata&&(!strcmp(branch.name,"network")||!strcmp(branch.name,"boards")))continue;
            if(t.stopped()){error="cancelled_or_offline";return false;}
            auto r=fetch(t,String("/rr_model?key=")+branch.name+"&flags=d5",0,"",branch.limit);
            if(!r.ok){
                if(!branch.required&&(r.status==404||r.status==400))continue;
                String detail;statusError(r,detail);
                error=String("printer_model_")+branch.name+"_"+detail;
                return false;
            }
            JsonDocument part(&allocator);
            // rr_model key=state returns result.status, not result.state.status.
            // Arrays (tools/inputs/etc.) likewise need their branch-level filter.
            JsonDocument branchFilter;
            branchFilter["result"].set(f[branch.name]);
            if(deserializeJson(part,r.body,DeserializationOption::Filter(branchFilter),DeserializationOption::NestingLimit(16))){
                error=String("printer_model_")+branch.name+"_invalid";return false;
            }
            JsonVariantConst value=part["result"];
            if(value.isNull()){
                if(!branch.required)continue;
                error=String("printer_model_")+branch.name+"_missing";return false;
            }
            doc["result"][branch.name].set(value);
        }
    }
    auto model=sbc_?doc.as<JsonVariantConst>():doc["result"].as<JsonVariantConst>();
    if(!normalize(model,s,error))return false;
    if(p.alignment=="rrf_bed_g")s.controls|=PrinterZTilt;
    if(!sbc_){
        if(refreshMetadata){metadataAt_=millis();metadataKnown_=true;}
        else {strlcpy(s.printerName,previousName.c_str(),sizeof(s.printerName));strlcpy(s.firmware,previousFirmware.c_str(),sizeof(s.firmware));}
    }
    if(!sbc_&&!model["seqs"]["reply"].isNull()){
        const uint32_t sequence=model["seqs"]["reply"].as<uint32_t>();
        if((!replyKnown_||sequence!=replySequence_)&&!t.stopped()){
            auto response=fetch(t,"/rr_reply",0,"",2048);
            if(response.ok){reply_=response.body.substring(0,512);replySequence_=sequence;replyKnown_=true;}
        }
    }return true;
}
bool RepRapPrinterAdapter::list(PrinterTransport& t,const PrinterProfile& p,const String& dir,String& json,String& error){
    if(!connect(t,p,error))return false;
    auto r=fetch(t,sbc_?"/machine/directory/"+encode(dir):
        "/rr_filelist?dir="+encode(dir)+"&first="+String(listOffset_)+"&max=32");
    if(statusError(r,error))return false;
    PrinterJsonAllocator allocator;JsonDocument doc(&allocator);if(!parse(r.body,doc,error))return false;
    if(!sbc_&&(doc["err"]|0)!=0){error="printer_directory_unavailable";return false;}
    auto files=sbc_?doc.as<JsonArrayConst>():doc["files"].as<JsonArrayConst>();
    if(!sbc_&&doc["files"].isNull()||sbc_&&!doc.is<JsonArray>()){error="printer_invalid_directory";return false;}
    const size_t first=sbc_?std::min<size_t>(listOffset_,files.size()):0;
    const size_t available=files.size()-first;
    const size_t count=std::min<size_t>(available,32);
    const int nextToken=doc["next"].is<int>()?doc["next"].as<int>():-1;
    const bool more=sbc_?(first+count<files.size()):(nextToken>=0?uint32_t(nextToken)>uint32_t(listOffset_):count==32);
    JsonDocument out;out["directory"]=dir;out["pending"]=false;
    out["offset"]=listOffset_;out["page_size"]=32;out["has_more"]=more;out["truncated"]=more;
    out["next_offset"]=more?(nextToken>int(listOffset_)?nextToken:int(listOffset_)+int(count)):0;
    if(sbc_)out["total_count"]=files.size();
    else if(doc["total"].is<uint32_t>())out["total_count"]=doc["total"].as<uint32_t>();
    else out["total_count"]=nullptr;
    out["sort"]="name_on_page"; // Server's global sort order is not guaranteed.
    // Sorting up to 32 entries never requires loading the full remote directory.
    JsonVariantConst ordered[32];
    for(size_t i=0;i<count;++i)ordered[i]=files[first+i];
    std::sort(ordered,ordered+count,[](JsonVariantConst a,JsonVariantConst b){
        const char* left=a["name"]|"";const char* right=b["name"]|"";
        return strcmp(left,right)<0;
    });
    auto entries=out["entries"].to<JsonArray>();
    for(size_t i=0;i<count;++i){auto f=ordered[i];const String name=f["name"]|"";const String path=dir+"/"+name;
        if(!printerPath(path.c_str(),(dir+"/").c_str())||name.indexOf('/')>=0)continue;
        auto entry=entries.add<JsonObject>();entry["name"]=name;entry["path"]=path;entry["directory"]=String(f["type"]|"")=="d";entry["size"]=f["size"]|0u;if(f["date"].is<const char*>())entry["date"]=f["date"];}
    serializeJson(out,json);if(json.length()>8192){error="printer_directory_limit";return false;}return true;
}
bool RepRapPrinterAdapter::upload(PrinterTransport& t,const PrinterProfile& p,const String& source,const String& target,String& error){
    if(!printerPath(target.c_str(),"0:/gcodes/")){error="printer_path_invalid";return false;}
    if(!connect(t,p,error))return false;
    auto r=t.upload(sbc_?"/machine/file/"+encode(target):"/rr_upload?name="+encode(target),session_,source,"","",sbc_?2:1);
    if(statusError(r,error))return false;
    if(!sbc_){JsonDocument doc;if(!parse(r.body,doc,error))return false;if(!doc["err"].is<int>()||doc["err"].as<int>()!=0){error="printer_upload_rejected";return false;}}
    return true;
}
bool RepRapPrinterAdapter::thumbnail(PrinterTransport& t,const PrinterProfile& p,const String& path,String& bytes,String& error){
    if(!printerPath(path.c_str(),"0:/gcodes/")){error="printer_path_invalid";return false;}
    if(!connect(t,p,error))return false;
    // rr_* compatibility endpoints also exist on DSF; keep the session in firmware.
    auto info=fetch(t,"/rr_fileinfo?name="+encode(path),0,"",16384);
    if(statusError(info,error))return false;
    JsonDocument d;if(!parse(info.body,d,error))return false;
    uint32_t offset=0;size_t best=0;
    PrinterThumbnailChoice choice{t.previewWidth,t.previewHeight};
    for(auto thumb:d["thumbnails"].as<JsonArrayConst>()){
        const String format=thumb["format"]|"";const size_t size=thumb["size"]|0u;
        const unsigned w=thumb["width"]|0u,h=thumb["height"]|0u;
        const uint32_t candidate=thumb["offset"]|0u;
        if((format=="png"||format=="qoi")&&size&&size<=65536&&candidate&&choice.consider(w,h)){best=w*h;offset=candidate;}
    }
    if(!best||!offset){error="printer_thumbnail_missing_or_limit";return false;}
    String encoded;
    for(unsigned page=0;page<48&&!t.stopped();++page){
        auto result=fetch(t,"/rr_thumbnail?name="+encode(path)+"&offset="+String(offset),0,"",16384);
        if(statusError(result,error))return false;d.clear();if(!parse(result.body,d,error))return false;
        if((d["err"]|1)!=0||!d["data"].is<const char*>()){error="printer_thumbnail_invalid";return false;}
        const String part=d["data"].as<String>();if(encoded.length()+part.length()>90000){error="printer_thumbnail_limit";return false;}encoded+=part;
        const uint32_t next=d["next"]|0u;
        if(!next){if(printerThumbnailBase64(encoded,bytes))return true;error="printer_thumbnail_invalid";return false;}
        if(next<=offset){error="printer_thumbnail_invalid";return false;}offset=next;
    }
    error="printer_thumbnail_timeout";return false;
}
bool RepRapPrinterAdapter::command(PrinterTransport& t,const PrinterProfile& p,const String& kind,JsonVariantConst a,PrinterStatus& s,String& reply,String& error){
    // Refresh on the worker immediately before validation, never trust stale UI data.
    if(!poll(t,p,s,error))return false;String code;
    if(kind=="pause"){if(strcmp(s.state,"processing")&&strcmp(s.state,"printing")){error="printer_not_printing";return false;}code="M25";}
    else if(kind=="resume"){if(strcmp(s.state,"paused")){error="printer_not_paused";return false;}code="M24";}
    else if(kind=="cancel"){if(!printerActive(s)){error="printer_not_printing";return false;}code=strcmp(s.state,"paused")?"M25\nM0":"M0";}
    else if(kind=="home"){const String axes=a["axes"]|"xyz";if(!printerHomeSafe(s,axes.c_str())){error="printer_not_idle_or_axis_unavailable";return false;}
        if(axes!="x"&&axes!="y"&&axes!="z"&&axes!="xy"&&axes!="xyz"){error="printer_argument_axes";return false;}
        code="G28";for(size_t letter=0;letter<axes.length();++letter){
            bool exists=false;for(size_t i=0;i<s.axisCount;++i)if(s.axes[i].letter==toupper(axes[letter]))exists=true;
            if(!exists){error="printer_axis_unavailable";return false;}code+=" ";code+=char(toupper(axes[letter]));}}
    else if(kind=="move"){const String axis=a["axis"]|"";const double distance=a["distance"]|NAN;
        if(axis.length()!=1||!a["relative"].as<bool>()||!printerJogSafe(s,toupper(axis[0]),distance)){error="printer_move_not_safe";return false;}
        // No persistent change to the HTTP channel's coordinate mode/feedrate.
        double feed=axis=="z"?300:1200;for(size_t i=0;i<s.axisCount;++i)if(s.axes[i].letter==toupper(axis[0]))feed=std::min(feed,s.axes[i].maxFeed);
        code="M120\nG91\nG1 ";code+=char(toupper(axis[0]));code+=String(distance,3);code+=" F"+String(feed,3)+"\nM121";}
    else if(kind=="bed_mesh"||kind=="z_tilt"||kind=="mesh_clear"){
        const uint32_t flag=kind=="bed_mesh"?PrinterMesh:kind=="z_tilt"?PrinterZTilt:PrinterMeshClear;
        if(!(s.controls&flag)||!printerCalibrationSafe(s)){error="printer_calibration_not_safe";return false;}
        code=kind=="bed_mesh"?"G29 S0":kind=="z_tilt"?"G32":"G29 S2";
    }else if(kind=="select_tool"){
        const auto* tool=printerFindTool(s,a["tool"]|"");
        if(!tool||tool->number<0||!printerCalibrationSafe(s)){error="printer_tool_not_safe";return false;}
        code=String("T")+tool->number;
    }else if(kind=="extrude"||kind=="set_flow_factor"){
        const auto* tool=printerFindTool(s,a["tool"]|"");
        if(!tool||!tool->activeKnown||!tool->active||tool->extruderCount!=1){error="printer_tool_not_active";return false;}
        if(kind=="extrude"){
            const double distance=a["distance"]|NAN,feed=a["feed"]|NAN;
            if(!printerExtrusionSafe(s,distance,feed)){error="printer_extrude_not_safe";return false;}
            code="M120\nM83\nG1 E"+String(distance,3)+" F"+String(feed,1)+"\nM121";
        }else {const double pct=a["percent"]|NAN;
            if((!printerIdle(s)&&!printerActive(s))||!std::isfinite(pct)||pct<10||pct>200){error="printer_argument_flow";return false;}
            code="M221 D"+String(tool->extruders[0])+" S"+String(pct,1);}
    }else if(kind=="motors_off"){
        if(!printerIdle(s)||!s.httpChannelIdle){error="printer_not_idle";return false;}code="M18";
    }else if(kind=="set_temperature"){const String which=a["heater"]|"";const auto* h=printerFindHeater(s,which.c_str());const double target=a["target"]|NAN;
        if(!h||!printerHeatSafe(s,*h,target,h->category==PrinterTemperatureKind::Bed||h->category==PrinterTemperatureKind::Chamber)){error="printer_temperature_not_safe";return false;}
        if(h->category==PrinterTemperatureKind::Bed&&h->group>=0)code=String("M140 P")+h->group+" S"+String(target,1);
        else if(h->category==PrinterTemperatureKind::Chamber&&h->group>=0)code=String("M141 P")+h->group+" S"+String(target,1);
        else {bool mapped=false;for(size_t i=0;i<s.toolCount;++i)if(s.tools[i].heaterCount==1&&s.tools[i].heaters[0]==h->index){code=String("M568 P")+s.tools[i].number+" S"+String(target,1)+(target>0?" A2":" A0");mapped=true;break;}
            if(!mapped){error="printer_heater_mapping_unsupported";return false;}}}
    else if(kind=="set_speed_factor"){double percent=a["percent"]|NAN;if(!printerActive(s)&&!printerIdle(s)||!std::isfinite(percent)||percent<10||percent>300){error="printer_argument_speed";return false;}code="M220 S"+String(percent,1);}
    else if(kind=="set_fan"){double percent=a["percent"]|NAN;int index=a["fan"]|-1;if(!std::isfinite(percent)||percent<0||percent>100||(!printerIdle(s)&&!printerActive(s))){error="printer_argument_fan";return false;}
        if(index<0)for(size_t i=0;i<s.fanCount;++i)if(!s.fans[i].automatic){index=s.fans[i].index;break;}
        bool safe=false;for(size_t i=0;i<s.fanCount;++i)if(s.fans[i].index==index&&!s.fans[i].automatic)safe=true;
        if(!safe){error="printer_fan_protected";return false;}code="M106 P"+String(index)+" S"+String(percent/100,3);}
    else if(kind=="start_file"||kind=="run_macro"){if(!printerIdle(s)){error="printer_not_idle";return false;}
        String path=a["path"]|a["name"]|"";const bool macro=kind=="run_macro";const String root=macro?"0:/macros/":"0:/gcodes/";
        if(path.startsWith("/"))path="0:"+path;else if(!path.startsWith("0:/"))path=root+path;
        if(path.length()!=strlen(path.c_str())||!printerPath(path.c_str(),root.c_str())){error="printer_path_invalid";return false;}
        code=String(macro?"M98 P\"":"M32 \"")+path+"\"";}
    else if(kind=="gcode"){
        // Explicit diagnostics only. Arbitrary raw code would bypass all safety checks.
        code=a["code"]|"";code.trim();code.toUpperCase();
        if(code!="M115"&&code!="M119"&&code!="M105"&&code!="M114"){error="printer_gcode_use_typed_command";return false;}
    }else{error="printer_command_unsupported";return false;}
    if(t.stopped()){error="cancelled";return false;}
    auto r=fetch(t,sbc_?"/machine/code?async=true":"/rr_gcode?gcode="+encode(code),sbc_?1:0,sbc_?code:String());
    if(statusError(r,error)){if(!r.status)error="printer_command_outcome_unknown";return false;}
    if(!sbc_){PrinterJsonAllocator allocator;JsonDocument doc(&allocator);if(!parse(r.body,doc,error))return false;
        if(doc["buff"].isNull()){error="printer_command_outcome_unknown";return false;}
        auto response=fetch(t,"/rr_reply");if(response.ok)reply=response.body.substring(0,512);
    }else reply=r.body.substring(0,512);
    // An HTTP acceptance is not evidence of completed motion/heating.
    if(reply.startsWith("Error:")||reply.indexOf("\nError:")>=0){error="printer_command_rejected";return false;}
    return true;
}
}
