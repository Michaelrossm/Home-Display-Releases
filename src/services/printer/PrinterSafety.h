#pragma once
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <initializer_list>
namespace hp {
// Transport-independent limits and safety rules, shared by future adapters.
constexpr uint32_t kPrinterSchemaVersion=2;
constexpr uint32_t kPrinterCommandQueueLimitMs=10000;
inline bool printerCommandQueueExpired(const char* kind,uint32_t queuedAt,uint32_t now){
    return std::strcmp(kind,"preview_file")&&std::strcmp(kind,"upload_file")&&
        uint32_t(now-queuedAt)>kPrinterCommandQueueLimitMs;
}
inline uint16_t printerDefaultPort(const char* protocol){return !std::strcmp(protocol,"moonraker")||!std::strcmp(protocol,"klipper")?7125:!std::strcmp(protocol,"octoprint")||!std::strcmp(protocol,"marlin_octoprint")?5000:80;}
enum class PrinterAvailability:uint8_t {NotLoaded,Available,Unavailable,Unsupported,Absent};
enum class PrinterTemperatureKind:uint8_t {Sensor,Hotend,Bed,Chamber,Host};
enum PrinterControl:uint32_t {PrinterMesh=1,PrinterZTilt=2,PrinterGantry=4,PrinterScrews=8,
    PrinterToolSelect=16,PrinterExtrude=32,PrinterFlow=64,PrinterMotorsOff=128,PrinterMeshClear=256};
inline bool printerCalibrationProfile(const char* v){return v&&(!strcmp(v,"none")||!strcmp(v,"marlin_bilinear")||!strcmp(v,"marlin_ubl"));}
inline bool printerAlignmentProfile(const char* v){return v&&(!strcmp(v,"none")||!strcmp(v,"marlin_g34")||!strcmp(v,"rrf_bed_g"));}
struct PrinterTemperature {
    char id[64]{},name[48]{};PrinterTemperatureKind category=PrinterTemperatureKind::Sensor;
    double current=NAN,target=NAN;int heater=-1;bool controllable=false,shared=false;
};
struct PrinterAxis { char letter=0; bool homed=false; double position=NAN,machinePosition=NAN,min=NAN,max=NAN,maxFeed=0; bool limits=false,homedKnown=false; };
struct PrinterHeater { int index=-1,tool=-1,sensor=-1,group=-1; double current=NAN,target=NAN,max=NAN; bool fault=false,limit=false,mappingSafe=true; char id[64]{},name[48]{};PrinterTemperatureKind category=PrinterTemperatureKind::Hotend; };
struct PrinterFan { int index=-1; double value=NAN,rpm=NAN; bool automatic=true;char id[64]{},name[48]{}; };
struct PrinterTool {int number=-1;char id[64]{},name[48]{};int heaters[8]{},extruders[8]{};uint8_t heaterCount=0,extruderCount=0;bool active=false,activeKnown=false;};
struct PrinterExtruder {int index=-1;char id[64]{},name[48]{};double position=NAN,flowFactor=NAN;};
struct PrinterStatus {
    static constexpr size_t kTemperatures=24,kHeaters=16,kTools=8,kExtruders=8;
    char state[24]{},filename[161]{};
    PrinterAxis axes[12]; size_t axisCount=0;
    PrinterHeater nozzle,bed;
    PrinterTemperature temperatures[kTemperatures];size_t temperatureCount=0;
    PrinterHeater heaters[kHeaters];size_t heaterCount=0;
    PrinterTool tools[kTools];size_t toolCount=0;
    PrinterExtruder extruders[kExtruders];size_t extruderCount=0;
    PrinterFan fans[16]; size_t fanCount=0;
    double progress=NAN,duration=NAN,remaining=NAN,speedFactor=NAN,flowFactor=NAN;
    uint32_t layer=0,layers=0;
    bool valid=false;
    bool httpMillimeters=false,httpChannelIdle=false;
    bool truncated=false,layerKnown=false,layersKnown=false,storageReady=false,storageKnown=false;
    PrinterAvailability temperatureAvailability=PrinterAvailability::NotLoaded,fanAvailability=PrinterAvailability::NotLoaded,axisAvailability=PrinterAvailability::NotLoaded,toolAvailability=PrinterAvailability::NotLoaded,extruderAvailability=PrinterAvailability::NotLoaded;
    char firmware[64]{},printerName[64]{};uint32_t updatedAt=0;
    uint32_t controls=0;double minExtrudeTemperature=NAN;bool canExtrude=false,calibrationHomes=false;
    uint32_t knownControls=0,configurableControls=0;
};
inline bool printerIdentity(const char* id,size_t max) {
    if(!id||!*id||std::strlen(id)>max)return false;
    for(const auto* p=id;*p;++p)if(!std::isalnum(static_cast<unsigned char>(*p))&&*p!='_'&&*p!='-')return false;
    return true;
}
inline bool printerPath(const char* path,const char* root) {
    if(!path||std::strlen(path)>160||std::strncmp(path,root,std::strlen(root)))return false;
    const char* p=path+std::strlen(root);if(!*p)return false;
    while(*p){const char* start=p;while(*p&&*p!='/'){if(static_cast<unsigned char>(*p)<32||*p=='"'||*p=='\''||*p=='\\'||*p==':'||*p==';')return false;++p;}
        const size_t n=p-start;if(!n||(n==1&&start[0]=='.')||(n==2&&start[0]=='.'&&start[1]=='.'))return false;
        if(*p&&!*++p)return false;
    }return true;
}
inline bool printerIdle(const PrinterStatus& s){return s.valid&&!std::strcmp(s.state,"idle");}
inline bool printerActive(const PrinterStatus& s){return s.valid&&(!std::strcmp(s.state,"processing")||!std::strcmp(s.state,"printing")||!std::strcmp(s.state,"paused"));}
inline bool printerAllHomed(const PrinterStatus& s){
    for(char axis: {'X','Y','Z'}){bool found=false;for(size_t i=0;i<s.axisCount;++i)
        if(s.axes[i].letter==axis)found=s.axes[i].homedKnown&&s.axes[i].homed;
        if(!found)return false;}return true;
}
inline bool printerCalibrationSafe(const PrinterStatus& s){return printerIdle(s)&&s.httpChannelIdle&&printerAllHomed(s);}
inline bool printerExtrusionSafe(const PrinterStatus& s,double distance,double feed){
    return printerIdle(s)&&s.httpChannelIdle&&s.httpMillimeters&&s.canExtrude&&
        std::isfinite(distance)&&distance!=0&&std::abs(distance)<=10&&std::isfinite(feed)&&feed>=1&&feed<=300;
}
inline bool printerJogSafe(const PrinterStatus& s,char axis,double distance) {
    if(!printerIdle(s)||!s.httpMillimeters||!s.httpChannelIdle||!std::isfinite(distance)||distance==0||std::abs(distance)>10)return false;
    for(size_t i=0;i<s.axisCount;++i){const auto& a=s.axes[i];if(a.letter==axis)return a.homed&&a.limits&&std::isfinite(a.maxFeed)&&a.maxFeed>0&&
        std::isfinite(a.machinePosition)&&a.machinePosition+distance>=a.min&&a.machinePosition+distance<=a.max;}
    return false;
}
inline bool printerHeatSafe(const PrinterStatus& s,const PrinterHeater& h,double target,bool bed) {
    return s.valid&&(printerIdle(s)||printerActive(s))&&h.index>=0&&!h.fault&&h.limit&&h.mappingSafe&&
        std::isfinite(h.current)&&std::isfinite(target)&&target>=0&&target<=h.max&&target<=(bed?150:350);
}
inline uint32_t printerBackoff(unsigned failures){return 5000u<<((failures>4?4:failures)- (failures?1:0));}
}
