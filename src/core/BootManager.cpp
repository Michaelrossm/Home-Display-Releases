#include "BootManager.h"
#include "../services/ConfigService.h"
#include "EventLog.h"
#include <esp_system.h>
namespace hp {
namespace {
bool isUnexpectedReset(esp_reset_reason_t reason){
    switch(reason){
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}
}
BootManager& BootManager::instance(){static BootManager b;return b;}
bool BootManager::begin(){auto&c=ConfigService::instance();state_.bootCount=c.getU32("boot_count",0)+1;state_.previousResetReason=c.getU32("reset_prev",0);const auto reset=esp_reset_reason();state_.resetReason=(int)reset;const uint32_t previousFailures=c.getU32("boot_fail",0);state_.failedBootCount=isUnexpectedReset(reset)?previousFailures+1:0;previousFailure_=c.getString("boot_reason","");state_.safeMode=state_.failedBootCount>=3;c.putU32("boot_count",state_.bootCount);c.putU32("boot_fail",state_.failedBootCount);c.putU32("reset_prev",state_.resetReason);return true;}
void BootManager::markHealthy(){auto&c=ConfigService::instance();state_.failedBootCount=0;state_.safeMode=false;c.putU32("boot_fail",0);c.putString("boot_reason","");}
void BootManager::markFailed(const char*r){previousFailure_=r?r:"unknown";ConfigService::instance().putString("boot_reason",previousFailure_);EventLog::instance().add("ERROR","BOOT_FAIL",previousFailure_);}
}
