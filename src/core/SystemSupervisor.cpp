#include "SystemSupervisor.h"
#include "EventLog.h"
namespace hp {
SystemSupervisor& SystemSupervisor::instance(){static SystemSupervisor s;return s;}
void SystemSupervisor::begin(){s_[(size_t)ServiceId::Ui].deadlineMs=500;s_[(size_t)ServiceId::Audio].deadlineMs=1000;s_[(size_t)ServiceId::Network].deadlineMs=2500;s_[(size_t)ServiceId::Storage].deadlineMs=2500;s_[(size_t)ServiceId::Media].deadlineMs=1500;}
void SystemSupervisor::heartbeat(ServiceId id){auto&x=s_[(size_t)id];x.lastHeartbeat=millis();x.seen=true;}
bool SystemSupervisor::healthy(ServiceId id)const{const auto&x=s_[(size_t)id];return x.seen&&(uint32_t)(millis()-x.lastHeartbeat)<=x.deadlineMs;}
bool SystemSupervisor::healthyCritical()const{return healthy(ServiceId::Ui)&&healthy(ServiceId::Storage)&&healthy(ServiceId::Media);}
void SystemSupervisor::loop(){if(millis()-lastCheck_<500)return;lastCheck_=millis();for(size_t i=0;i<(size_t)ServiceId::Count;++i){auto&x=s_[i];if(x.seen&&(uint32_t)(millis()-x.lastHeartbeat)>x.deadlineMs){if(x.missed++==0){++watchdogEvents_;EventLog::instance().add("WARN","SERVICE_STALL","Service heartbeat missed: "+String(i));}}else x.missed=0;}}
}
