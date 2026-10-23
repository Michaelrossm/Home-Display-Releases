#pragma once
#include <Arduino.h>
namespace hp {
struct BootState{uint32_t bootCount=0;uint32_t failedBootCount=0;int resetReason=0;int previousResetReason=0;bool safeMode=false;};
class BootManager{public:static BootManager&instance();bool begin();void markHealthy();void markFailed(const char*reason);const BootState&state()const{return state_;}const String&previousFailure()const{return previousFailure_;}private:BootState state_{};String previousFailure_;};
}
