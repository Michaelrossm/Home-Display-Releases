#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
namespace hp {
enum class EnergyQuality : uint8_t { Unavailable,Calculated,Measured };
struct EnergyValue {
    float value=NAN;
    EnergyQuality quality=EnergyQuality::Unavailable;
    char source[32]{};
    bool available() const { return quality!=EnergyQuality::Unavailable&&std::isfinite(value); }
};
struct EnergyPhase { EnergyValue voltage,current,power; };
struct EnergySnapshot {
    EnergyValue gridPower,housePower,pvPower,batteryPower,batterySoc;
    EnergyValue gridImportTotal,gridExportTotal;
    EnergyValue pvEnergyToday,houseEnergyToday,batteryChargeToday,batteryDischargeToday;
    std::array<EnergyPhase,3> phase;
    int64_t timestamp=0;
    uint32_t updatedMs=0,revision=0;
    bool online=false;
    char status[48]{},source[32]{};
};
// Ignore refresh timestamps, never struct padding. Missing NaN values compare
// equal so an unchanged offline provider does not flood the UI event queue.
inline bool sameEnergyValue(const EnergyValue& a,const EnergyValue& b){
    return a.quality==b.quality&&!strcmp(a.source,b.source)&&
        (a.value==b.value||(std::isnan(a.value)&&std::isnan(b.value)));
}
inline bool sameEnergyContent(const EnergySnapshot& a,const EnergySnapshot& b){
    if(a.online!=b.online||strcmp(a.status,b.status)||strcmp(a.source,b.source))return false;
    const EnergyValue* av[]={&a.gridPower,&a.housePower,&a.pvPower,&a.batteryPower,&a.batterySoc,
        &a.gridImportTotal,&a.gridExportTotal,&a.pvEnergyToday,&a.houseEnergyToday,&a.batteryChargeToday,&a.batteryDischargeToday};
    const EnergyValue* bv[]={&b.gridPower,&b.housePower,&b.pvPower,&b.batteryPower,&b.batterySoc,
        &b.gridImportTotal,&b.gridExportTotal,&b.pvEnergyToday,&b.houseEnergyToday,&b.batteryChargeToday,&b.batteryDischargeToday};
    for(unsigned i=0;i<11;++i)if(!sameEnergyValue(*av[i],*bv[i]))return false;
    for(unsigned i=0;i<3;++i)if(!sameEnergyValue(a.phase[i].voltage,b.phase[i].voltage)||
        !sameEnergyValue(a.phase[i].current,b.phase[i].current)||!sameEnergyValue(a.phase[i].power,b.phase[i].power))return false;
    return true;
}
struct EnergyHistoryPoint {
    int64_t timestamp=0;
    float values[7]={NAN,NAN,NAN,NAN,NAN,NAN,NAN};
    uint16_t resolutionSeconds=0,quality=0;
};
class EnergyHistoryProvider {
public:
    using Visitor=std::function<bool(const EnergyHistoryPoint&)>;
    virtual ~EnergyHistoryProvider()=default;
    virtual bool supportsHistoryBackfill() const {return false;}
    virtual const char* historySource() const {return "";}
    virtual bool fetchHistory(int64_t,int64_t,uint16_t,const Visitor&){return false;}
};
class EnergyProvider : public EnergyHistoryProvider {
public:
    virtual ~EnergyProvider()=default;
    virtual const char* name() const=0;
    virtual int priority() const=0;
    // Must return a cached snapshot, never perform I/O from this method.
    virtual bool snapshot(EnergySnapshot& result) const=0;
};
inline void chooseEnergyValue(EnergyValue& target,const EnergyValue& candidate,int priority,int& selectedPriority) {
    if(!candidate.available())return;
    if(!target.available() || candidate.quality>target.quality ||
       (candidate.quality==target.quality&&priority>selectedPriority)) {
        target=candidate;selectedPriority=priority;
    }
}
}
