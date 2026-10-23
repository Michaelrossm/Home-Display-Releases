#pragma once
#include "PrinterSafety.h"
#include <algorithm>
namespace hp {
enum class PrinterOperationPhase:uint8_t{None,Queued,Accepted,Running,Completed,Failed,Unknown};
inline const char* printerOperationName(PrinterOperationPhase p){switch(p){case PrinterOperationPhase::Queued:return "queued";case PrinterOperationPhase::Accepted:return "accepted";case PrinterOperationPhase::Running:return "running";case PrinterOperationPhase::Completed:return "completed";case PrinterOperationPhase::Failed:return "failed";case PrinterOperationPhase::Unknown:return "outcome_unknown";default:return "none";}}
struct PrinterOperation {
    char action[33]{},component[65]{};PrinterOperationPhase phase=PrinterOperationPhase::None;
    uint32_t id=0,changed=0,started=0;double target=NAN;bool observedBusy=false;
    void begin(uint32_t job,const char* kind,uint32_t now){*this={};id=job;strncpy(action,kind,sizeof(action)-1);started=changed=now;phase=PrinterOperationPhase::Queued;}
    bool active()const{return phase==PrinterOperationPhase::Queued||phase==PrinterOperationPhase::Accepted||phase==PrinterOperationPhase::Running;}
    bool set(PrinterOperationPhase next,uint32_t now){if(phase==next)return false;phase=next;changed=now;return true;}
    bool observe(const PrinterStatus& s,uint32_t now){
        if(!active()||phase==PrinterOperationPhase::Queued)return false;
        if(!s.valid||!strcmp(s.state,"error"))return set(PrinterOperationPhase::Failed,now);
        if(uint32_t(now-started)>600000)return set(PrinterOperationPhase::Unknown,now);
        bool confirmed=false;
        if(!strcmp(action,"pause"))confirmed=!strcmp(s.state,"paused");
        else if(!strcmp(action,"resume")||!strcmp(action,"start_file"))confirmed=!strcmp(s.state,"printing")||!strcmp(s.state,"processing");
        else if(!strcmp(action,"cancel"))confirmed=printerIdle(s);
        else if(!strcmp(action,"select_tool")){for(size_t i=0;i<s.toolCount;++i)confirmed|=!strcmp(component,s.tools[i].id)&&s.tools[i].activeKnown&&s.tools[i].active;}
        else if(!strcmp(action,"set_temperature")){
            const PrinterHeater* h=nullptr;for(size_t i=0;i<s.heaterCount;++i)if(!strcmp(component,s.heaters[i].id))h=&s.heaters[i];
            if(!strcmp(component,"nozzle"))h=&s.nozzle;if(!strcmp(component,"bed"))h=&s.bed;
            confirmed=h&&!h->fault&&std::isfinite(target)&&std::isfinite(h->target)&&std::abs(h->target-target)<0.5&&
                (target==0||(std::isfinite(h->current)&&std::abs(h->current-target)<=2));
        }else if(!strcmp(action,"set_speed_factor"))confirmed=std::isfinite(target)&&std::abs(s.speedFactor-target)<0.5;
        else if(!strcmp(action,"set_flow_factor")){confirmed=!*component&&std::isfinite(target)&&std::abs(s.flowFactor-target)<0.5;
            for(size_t i=0;i<s.toolCount;++i)if(!strcmp(component,s.tools[i].id)&&s.tools[i].extruderCount==1)
                for(size_t j=0;j<s.extruderCount;++j)if(s.extruders[j].index==s.tools[i].extruders[0])confirmed|=std::isfinite(target)&&std::abs(s.extruders[j].flowFactor-target)<0.5;}
        // Channel-idle alone never proves that a probe/calibration/macro succeeded.
        if(confirmed)return set(PrinterOperationPhase::Completed,now);
        if(!s.httpChannelIdle||!printerIdle(s)){observedBusy=true;return set(PrinterOperationPhase::Running,now);}
        if(observedBusy)return set(PrinterOperationPhase::Unknown,now);
        return false;
    }
};
struct PrinterTemperatureHistory {
    static constexpr size_t channels=PrinterStatus::kTemperatures,points=60;
    struct Point {uint32_t ms=0;float current=NAN,target=NAN;};
    struct Channel {char id[64]{};Point data[points];uint8_t count=0,next=0;};
    Channel series[channels];uint32_t sampled=0,revision=0;
    bool sample(const PrinterStatus& s,uint32_t now){if(!s.valid||(revision&&uint32_t(now-sampled)<5000))return false;sampled=now;
        for(size_t i=0;i<s.temperatureCount;++i){const auto& t=s.temperatures[i];Channel* channel=nullptr;
            for(auto& c:series)if(!strcmp(c.id,t.id)){channel=&c;break;}
            if(!channel)for(auto& c:series)if(!*c.id){strncpy(c.id,t.id,sizeof(c.id)-1);channel=&c;break;}
            if(!channel)continue;channel->data[channel->next]={now,float(t.current),float(t.target)};channel->next=(channel->next+1)%points;if(channel->count<points)++channel->count;
        }++revision;return true;
    }
};
}
