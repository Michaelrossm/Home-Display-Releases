#include "OtaService.h"
#include "NetworkService.h"
#include "../core/EventBus.h"
#include "../core/BootHealthCheck.h"
#include "../core/BootManager.h"
#include "../core/EventLog.h"
#include "../infrastructure/TransactionJournal.h"
#include <esp_ota_ops.h>
namespace hp {
OtaService& OtaService::instance(){static OtaService o;return o;}
bool OtaService::begin(){lastError_="";state_=OtaState::Ready;const esp_partition_t*r=esp_ota_get_running_partition();if(!r){fail("running partition unavailable");return false;}esp_ota_img_states_t st;if(esp_ota_get_state_partition(r,&st)==ESP_OK&&st==ESP_OTA_IMG_PENDING_VERIFY){pendingValidation_=true;state_=OtaState::PendingBootValidation;TransactionJournal::instance().append(UpdateState::HealthChecking);}return true;}
void OtaService::loop(){}
bool OtaService::canCheckForUpdates()const{return NetworkService::instance().online();}
bool OtaService::commitIfHealthy(const BootHealthReport&r){if(!pendingValidation_)return true;state_=OtaState::HealthChecking;if(!r.ok()){lastError_="health check: "+r.firstFailure;EventLog::instance().add("WARN","OTA_HEALTH_WAIT",lastError_);return false;}if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK){fail("could not validate running OTA image");TransactionJournal::instance().append(UpdateState::RecoveryRequired);return false;}pendingValidation_=false;state_=OtaState::Ready;TransactionJournal::instance().append(UpdateState::Committed);BootManager::instance().markHealthy();EventLog::instance().add("INFO","OTA_COMMIT","Boot health passed; firmware committed");EventBus::instance().publish({EventType::OtaFinished,nullptr});return true;}
void OtaService::fail(const char*r){state_=OtaState::Failed;lastError_=r?r:"unknown OTA error";EventLog::instance().add("ERROR","OTA_FAIL",lastError_);}
}
