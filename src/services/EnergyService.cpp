#include "EnergyService.h"
#include "TimeService.h"
#include "EnergyHistoryService.h"
#include "../apps/power/engine/PowerMeterClient.h"
#include "../apps/lua/LuaEvents.h"
namespace hp {
namespace {
void measured(EnergyValue& value,float number,const char* source) {
    if(!std::isfinite(number))return;
    value.value=number;value.quality=EnergyQuality::Measured;strlcpy(value.source,source,sizeof(value.source));
}
void merge(EnergySnapshot& result,const EnergySnapshot& source,int priority,int* selected) {
    if(!source.online)return;
    EnergyValue* target[]={&result.gridPower,&result.housePower,&result.pvPower,&result.batteryPower,&result.batterySoc,
        &result.gridImportTotal,&result.gridExportTotal,&result.pvEnergyToday,&result.houseEnergyToday,&result.batteryChargeToday,&result.batteryDischargeToday};
    const EnergyValue* input[]={&source.gridPower,&source.housePower,&source.pvPower,&source.batteryPower,&source.batterySoc,
        &source.gridImportTotal,&source.gridExportTotal,&source.pvEnergyToday,&source.houseEnergyToday,&source.batteryChargeToday,&source.batteryDischargeToday};
    for(unsigned i=0;i<11;++i)chooseEnergyValue(*target[i],*input[i],priority,selected[i]);
    for(unsigned i=0;i<3;++i) {
        chooseEnergyValue(result.phase[i].voltage,source.phase[i].voltage,priority,selected[11+i*3]);
        chooseEnergyValue(result.phase[i].current,source.phase[i].current,priority,selected[12+i*3]);
        chooseEnergyValue(result.phase[i].power,source.phase[i].power,priority,selected[13+i*3]);
    }
    result.online=true;
    if(source.timestamp>result.timestamp)result.timestamp=source.timestamp;
    if(source.updatedMs>result.updatedMs)result.updatedMs=source.updatedMs;
}
}
EnergyService& EnergyService::instance(){static EnergyService service;return service;}
bool EnergyService::addProvider(EnergyProvider* provider) {
    if(!provider||count_>=8)return false;
    for(size_t i=0;i<count_;++i)if(providers_[i]==provider||!strcmp(providers_[i]->name(),provider->name()))return false;
    providers_[count_++]=provider;return true;
}
void EnergyService::publishPower(const PowerMeterData& data) {
    EnergySnapshot result;result.online=data.online;result.timestamp=TimeService::instance().nowUtc();result.updatedMs=data.updatedMs;
    const char* source=data.nativeStatus?"irtracker":"shelly";strlcpy(result.source,source,sizeof(result.source));
    strlcpy(result.status,data.online?"online":"offline",sizeof(result.status));
    if(data.online) {
        measured(result.gridPower,data.totalPower,source);
        if(data.countersValid){measured(result.gridImportTotal,data.totalImportKwh,source);measured(result.gridExportTotal,data.totalExportKwh,source);}
        for(unsigned i=0;i<3;++i){measured(result.phase[i].voltage,data.voltage[i],source);measured(result.phase[i].current,data.current[i],source);measured(result.phase[i].power,data.power[i],source);}
    }
    power_=result;refresh();
}
void EnergyService::refresh() {
    if(!mutex_)mutex_=xSemaphoreCreateMutex();if(!mutex_)return;
    EnergySnapshot result;int priorities[20];for(auto& value:priorities)value=INT32_MIN;
    merge(result,power_,100,priorities);
    for(size_t i=0;i<count_;++i){EnergySnapshot value;if(providers_[i]->snapshot(value)&&static_cast<uint32_t>(millis()-value.updatedMs)<=30000)merge(result,value,providers_[i]->priority(),priorities);}
    strlcpy(result.status,result.online?"online":"unavailable",sizeof(result.status));
    strlcpy(result.source,count_?"combined":power_.source,sizeof(result.source));
    xSemaphoreTake(mutex_,portMAX_DELAY);
    const bool changed=!sameEnergyContent(result,snapshot_);
    result.revision=snapshot_.revision+(changed?1:0);snapshot_=result;xSemaphoreGive(mutex_);
    if(changed)publishLuaEvent(nullptr,"energy.updated");
    EnergyHistoryService::instance().record(result);
}
bool EnergyService::trySnapshot(EnergySnapshot& result) {
    if(!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;
    result=snapshot_;xSemaphoreGive(mutex_);
    if(static_cast<uint32_t>(millis()-result.updatedMs)>30000){result.online=false;strlcpy(result.status,"stale",sizeof(result.status));}
    return true;
}
}
