#pragma once
#include <Arduino.h>
namespace hp {
enum class ServiceId:uint8_t{Ui,Audio,Network,Storage,Media,Count};
struct ServiceHealth{uint32_t lastHeartbeat=0;uint32_t deadlineMs=2000;uint32_t missed=0;bool seen=false;};
class SystemSupervisor{public:static SystemSupervisor&instance();void begin();void heartbeat(ServiceId);void loop();bool healthy(ServiceId)const;bool healthyCritical()const;const ServiceHealth&status(ServiceId id)const{return s_[(size_t)id];}uint32_t watchdogEvents()const{return watchdogEvents_;}private:ServiceHealth s_[(size_t)ServiceId::Count]{};uint32_t watchdogEvents_=0,lastCheck_=0;};
}
