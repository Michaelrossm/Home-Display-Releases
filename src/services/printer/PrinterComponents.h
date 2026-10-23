#pragma once
#include "PrinterAdapter.h"
#include <memory>
#include <new>
#include <esp_heap_caps.h>
namespace hp {
inline double printerNumber(JsonVariantConst v){return v.is<double>()&&std::isfinite(v.as<double>())?v.as<double>():NAN;}
inline String printerEncode(const String& s){String out;out.reserve(s.length()*3);const char hex[]="0123456789ABCDEF";
    for(size_t i=0;i<s.length();++i){const unsigned char c=s[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);else {out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
inline bool printerIndexedName(const char* name,const char* prefix,int& index){
    const size_t size=strlen(prefix);if(strncmp(name,prefix,size))return false;const char* p=name+size;if(!*p)return false;
    index=0;for(;*p;++p){if(*p<'0'||*p>'9'||index>6553)return false;index=index*10+*p-'0';if(index>65535)return false;}return true;
}
inline PrinterTemperature* printerTemperature(PrinterStatus& s,const char* id,const char* name,PrinterTemperatureKind kind,double current,double target=NAN,int heater=-1,bool controllable=false){
    if(!id||!*id||strlen(id)>=sizeof(PrinterTemperature::id)){s.truncated=true;return nullptr;}
    PrinterTemperature* t=nullptr;for(size_t i=0;i<s.temperatureCount;++i)if(!strcmp(s.temperatures[i].id,id))t=&s.temperatures[i];
    if(!t){if(s.temperatureCount>=s.kTemperatures){s.truncated=true;return nullptr;}t=&s.temperatures[s.temperatureCount++];}
    strlcpy(t->id,id,sizeof(t->id));if(strlcpy(t->name,name&&*name?name:id,sizeof(t->name))>=sizeof(t->name))s.truncated=true;
    t->category=kind;t->current=current;t->target=target;t->heater=heater;t->controllable=controllable;return t;
}
inline const PrinterHeater* printerFindHeater(const PrinterStatus& s,const char* id){
    if(!id)return nullptr;if(!strcmp(id,"bed"))return s.bed.index>=0?&s.bed:nullptr;if(!strcmp(id,"nozzle"))return s.nozzle.index>=0?&s.nozzle:nullptr;
    for(size_t i=0;i<s.heaterCount;++i)if(!strcmp(s.heaters[i].id,id))return &s.heaters[i];return nullptr;
}
inline const PrinterTool* printerFindTool(const PrinterStatus& s,const char* id){
    if(id)for(size_t i=0;i<s.toolCount;++i)if(!strcmp(s.tools[i].id,id))return &s.tools[i];return nullptr;
}
inline void printerWriteControls(JsonObject object,const PrinterStatus& s){
    object["extrusion_ready"]=s.canExtrude;
    object["homed"]=printerAllHomed(s);
    object["channel_idle"]=s.httpChannelIdle;
    object["calibration_homes"]=s.calibrationHomes;
    struct Control{const char* name;uint32_t flag;};
    for(const auto& c: {Control{"bed_mesh",PrinterMesh},{"z_tilt",PrinterZTilt},{"gantry_level",PrinterGantry},
        {"screws_tilt",PrinterScrews},{"select_tool",PrinterToolSelect},{"extrude",PrinterExtrude},
        {"set_flow_factor",PrinterFlow},{"motors_off",PrinterMotorsOff},{"mesh_clear",PrinterMeshClear}})
        object[c.name]=bool(s.controls&c.flag);
}
inline bool printerHomeSafe(const PrinterStatus& s,const char* axes){
    if(!printerIdle(s)||!s.httpChannelIdle||!axes||!*axes)return false;
    for(const char* p=axes;*p;++p){bool found=false;for(size_t i=0;i<s.axisCount;++i)if(s.axes[i].letter==toupper(static_cast<unsigned char>(*p)))found=true;if(!found)return false;}return true;
}
// Large operational snapshots are never placed on the 12 KiB NetworkWorker stack.
struct PrinterStatusDelete {void operator()(PrinterStatus* p)const{if(p){p->~PrinterStatus();heap_caps_free(p);}}};
using PrinterStatusPtr=std::unique_ptr<PrinterStatus,PrinterStatusDelete>;
inline PrinterStatusPtr printerStatusBuffer(){void* p=heap_caps_malloc(sizeof(PrinterStatus),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);return PrinterStatusPtr(p?new(p) PrinterStatus:nullptr);}
inline void printerFinalize(PrinterStatus& s){s.updatedAt=millis();if(s.temperatureAvailability==PrinterAvailability::NotLoaded)s.temperatureAvailability=s.temperatureCount?PrinterAvailability::Available:PrinterAvailability::Absent;}
inline const char* printerAvailability(PrinterAvailability v){switch(v){case PrinterAvailability::Available:return "available";case PrinterAvailability::Unavailable:return "unavailable";case PrinterAvailability::Unsupported:return "unsupported";case PrinterAvailability::Absent:return "absent";default:return "not_loaded";}}
inline const char* printerCategory(PrinterTemperatureKind v){switch(v){case PrinterTemperatureKind::Hotend:return "hotend";case PrinterTemperatureKind::Bed:return "bed";case PrinterTemperatureKind::Chamber:return "chamber";case PrinterTemperatureKind::Host:return "host";default:return "sensor";}}
template<class Value> inline void printerValue(Value v,double n){if(std::isfinite(n))v.set(n);else v.set(nullptr);}
inline void printerWriteComponents(JsonObject d,const PrinterStatus& s){
    printerWriteControls(d["controls"].to<JsonObject>(),s);
    d["schema_version"]=kPrinterSchemaVersion;d["truncated"]=s.truncated;d["firmware"]=s.firmware;d["printer_name"]=s.printerName;
    auto availability=d["availability"].to<JsonObject>();availability["temperatures"]=printerAvailability(s.temperatureAvailability);availability["fans"]=printerAvailability(s.fanAvailability);availability["axes"]=printerAvailability(s.axisAvailability);availability["tools"]=printerAvailability(s.toolAvailability);availability["extruders"]=printerAvailability(s.extruderAvailability);
    auto temperatures=d["temperatures"].to<JsonArray>();for(size_t i=0;i<s.temperatureCount;++i){const auto& t=s.temperatures[i];auto v=temperatures.add<JsonObject>();v["id"]=t.id;v["name"]=t.name;v["category"]=printerCategory(t.category);printerValue(v["current"],t.current);printerValue(v["target"],t.target);v["unit"]="C";v["valid"]=std::isfinite(t.current);v["controllable"]=t.controllable;v["heater_index"]=t.heater;}
    auto heaters=d["heaters"].to<JsonArray>();for(size_t i=0;i<s.heaterCount;++i){const auto& h=s.heaters[i];auto v=heaters.add<JsonObject>();v["id"]=h.id;v["name"]=h.name;v["index"]=h.index;v["sensor_index"]=h.sensor;v["category"]=printerCategory(h.category);printerValue(v["current"],h.current);printerValue(v["target"],h.target);printerValue(v["max"],h.max);v["fault"]=h.fault;v["limit_known"]=h.limit;v["controllable"]=h.mappingSafe&&h.limit&&!h.fault&&std::isfinite(h.current);}
    auto tools=d["tools"].to<JsonArray>();for(size_t i=0;i<s.toolCount;++i){const auto& t=s.tools[i];auto v=tools.add<JsonObject>();v["id"]=t.id;v["name"]=t.name;v["number"]=t.number;if(t.activeKnown)v["active"]=t.active;else v["active"]=nullptr;auto heaters=v["heaters"].to<JsonArray>();for(unsigned j=0;j<t.heaterCount;++j)heaters.add(t.heaters[j]);auto extruders=v["extruders"].to<JsonArray>();for(unsigned j=0;j<t.extruderCount;++j)extruders.add(t.extruders[j]);}
    auto extruders=d["extruders"].to<JsonArray>();for(size_t i=0;i<s.extruderCount;++i){const auto& e=s.extruders[i];auto v=extruders.add<JsonObject>();v["id"]=e.id;v["name"]=e.name;v["index"]=e.index;printerValue(v["position"],e.position);printerValue(v["flow_factor"],e.flowFactor);}
    for(size_t i=0;i<s.temperatureCount;++i)for(size_t j=0;j<s.heaterCount;++j)if(s.temperatures[i].heater==s.heaters[j].index)temperatures[i]["heater_id"]=s.heaters[j].id;
    for(size_t i=0;i<s.heaterCount;++i){const auto& h=s.heaters[i];heaters[i]["control_reason"]=h.fault?"fault":!h.mappingSafe?"unsafe_mapping":!h.limit?"unknown_limit":!std::isfinite(h.current)?"missing_reading":"";}
    for(size_t i=0;i<s.toolCount;++i){auto heatIds=tools[i]["heater_ids"].to<JsonArray>();auto driveIds=tools[i]["extruder_ids"].to<JsonArray>();
        for(size_t j=0;j<s.tools[i].heaterCount;++j)for(size_t k=0;k<s.heaterCount;++k)if(s.tools[i].heaters[j]==s.heaters[k].index)heatIds.add(s.heaters[k].id);
        for(size_t j=0;j<s.tools[i].extruderCount;++j)for(size_t k=0;k<s.extruderCount;++k)if(s.tools[i].extruders[j]==s.extruders[k].index)driveIds.add(s.extruders[k].id);}
    auto capabilities=d["capabilities"].to<JsonObject>();bool jog=false,heat=false,fan=false;for(size_t i=0;i<s.axisCount;++i)jog|=s.axes[i].homed&&s.axes[i].limits&&s.axes[i].maxFeed>0;capabilities["jog"]=s.httpMillimeters&&s.httpChannelIdle&&jog;for(size_t i=0;i<s.heaterCount;++i)heat|=s.heaters[i].mappingSafe&&s.heaters[i].limit&&!s.heaters[i].fault&&std::isfinite(s.heaters[i].current);capabilities["heating"]=heat;for(size_t i=0;i<s.fanCount;++i)fan|=!s.fans[i].automatic;capabilities["fans"]=fan;
    auto storage=d["storage"].to<JsonObject>();if(s.storageKnown)storage["ready"]=s.storageReady;else storage["ready"]=nullptr;
}
}
