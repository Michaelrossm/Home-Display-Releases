#include <Arduino.h>
#include <esp_heap_caps.h>
#include "HomePanelConfig.h"
#include "BuildInfo.h"
#include "BoardConfig.h"
#include "services/UsbStorageService.h"
#include "core/AppManager.h"
#include "core/BackgroundScheduler.h"
#include "core/BootManager.h"
#include "core/SystemSupervisor.h"
#include "core/BootHealthCheck.h"
#include "core/RecoveryManager.h"
#include "core/EventLog.h"
#include "infrastructure/TransactionJournal.h"
#include "infrastructure/AssetManager.h"
#include "services/ConfigService.h"
#include "services/LanguageService.h"
#include "services/RegionService.h"
#include "services/SettingsService.h"
#include "services/SecurityService.h"
#include "services/NetworkService.h"
#include "services/NetworkWorker.h"
#include "services/StorageService.h"
#include "services/TimeService.h"
#include "services/IdleService.h"
#include "services/OtaService.h"
#include "services/UpdateService.h"
#include "services/PowerManager.h"
#include "ui/ThemeCatalog.h"
#include "services/AudioHardwareService.h"
#include "services/AmbientLightService.h"
#include "services/BluetoothService.h"
#include "services/NasService.h"
#include "services/FileTransferService.h"
#include "services/WebServerService.h"
#include "services/RecoveryWebService.h"
#include "services/C6UpdateService.h"
#include "services/EthernetService.h"
#include "services/CalendarSyncService.h"
#include "apps/calendar/CalendarReminderService.h"
#include "services/DiagnosticsService.h"
#include "hardware/DisplayManager.h"
#include "ui/UiShell.h"
#include "ui/RecoveryUI.h"
#include "ui/SecurityUi.h"
#include "ui/HomeReorderController.h"

#include "apps/AppServices.h"
#include "apps/settings/SettingsApp.h"
#include "apps/browser/BrowserApp.h"
#include "apps/browser/WebAppSettingsApp.h"
#include "apps/files/FileExplorerApp.h"
#include "apps/calendar/CalendarApp.h"
#include "apps/intercom/IntercomApp.h"
#include "apps/phonebook/PhonebookApp.h"
#include "apps/calculator/CalculatorApp.h"
#include "apps/photos/PhotoApp.h"
#include "apps/music/MusicApp.h"
#include "ui/BootSplash.h"

namespace {
hp::SettingsApp settingsApp;
hp::BrowserApp browserApp;
hp::WebAppSettingsApp webAppSettingsApp;
bool recoveryMode=false,bootCommitted=false;uint32_t lastHealth=0;

bool registerApps(){
 auto& am=hp::AppManager::instance();static hp::FileExplorerApp fileExplorerApp;
 static hp::CalendarApp calendarApp;
#if HOMEPANEL_FEATURE_INTERCOM
 static hp::IntercomApp intercomApp;
#endif
 static hp::PhonebookApp phonebookApp;
 static hp::CalculatorApp calculatorApp;
 static hp::PhotoApp photoApp;
 static hp::MusicApp musicApp;
 hp::IApp* const apps[]={&browserApp,&webAppSettingsApp,&settingsApp,&fileExplorerApp,&calendarApp,
#if HOMEPANEL_FEATURE_INTERCOM
 &intercomApp,
#endif
 &phonebookApp,&calculatorApp,&photoApp,&musicApp};
 bool ok=true;
 for(auto* app:apps){
  if(am.registerApp(app)) continue;
  hp::EventLog::instance().add("ERROR","APP_INIT",app->id());
  ok=false;
 }
 return ok;
}
bool registerJobs(){const bool logOk=hp::EventLog::instance().deferPersistence();return hp::BackgroundScheduler::instance().begin()&&logOk;}
void enterRecovery(const char*reason){
 hp::BootSplash::hide();
 recoveryMode=true;
 if(reason)hp::BootManager::instance().markFailed(reason);
 // Port 8088 must remain available even when AppFS/Lua/UI startup is broken.
 if(!hp::RecoveryWebService::instance().begin())
  hp::EventLog::instance().add("ERROR","RECOVERY_WEB","Recovery web server failed to start");
 hp::DisplayUiGuard guard(5000);
 if(guard){hp::RecoveryUI::instance().show(lv_screen_active());hp::SecurityUi::sync();}
}
}

void setup(){
 Serial.begin(115200);delay(150);
 Serial.printf("\n%s %s (%s)\n%s\n",hp::build::kProduct,hp::build::kVersion,hp::build::kProfile,hp::board::kName);
 hp::ConfigService::instance().begin(); // BootHealth counters must use an opened NVS namespace.
 hp::RegionService::instance().begin();
 if(!hp::LanguageService::instance().begin())Serial.println("[LANG] German literal fallback active");
 if(!hp::BootManager::instance().begin())Serial.println("[BOOT] config/boot state unavailable");
 hp::SystemSupervisor::instance().begin();
 const bool storageOk=hp::StorageService::instance().begin();
 hp::EventLog::instance().begin(storageOk);
 hp::EventLog::instance().add("INFO","BOOT","Firmware "+String(hp::build::kVersion)+", reset="+String(hp::BootManager::instance().state().resetReason));
 const bool journalOk=hp::TransactionJournal::instance().begin();
 hp::StorageService::instance().applyBootSdFormat(storageOk&&journalOk&&!hp::RecoveryManager::instance().required()&&
   (hp::TransactionJournal::instance().state()==hp::UpdateState::Idle||hp::TransactionJournal::instance().state()==hp::UpdateState::Committed));
 const bool assetsOk=hp::AssetManager::instance().begin();
 hp::UpdateService::instance().begin();

 // Settings/security/power/display and the minimum network path are initialized
 // before deciding whether to stay in recovery. This makes the recovery web UI
 // reachable on LAN/WLAN instead of returning before networking has started.
 const bool settingsOk=hp::SettingsService::instance().begin();
 hp::ThemeCatalog::instance().begin(); // SD themes loaded before any LVGL lock exists.
 if(!hp::SecurityService::instance().begin())Serial.println("[BOOT] security unavailable; UI stays locked");
 const bool powerOk=settingsOk&&hp::PowerManager::instance().begin();
 const bool displayOk=hp::DisplayManager::instance().begin();
 const bool displayPerformanceOk=powerOk&&displayOk; // DisplayManager owns the guarded render-frequency lock.
 hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Storage);
 hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Media);
 if(!displayOk){hp::BootManager::instance().markFailed("display_init");return;}
 hp::BootSplash::show();

 const bool networkWorkerOk=hp::NetworkWorker::instance().begin();
 const bool ethernetOk=hp::EthernetService::instance().begin();
 const bool networkOk=hp::NetworkService::instance().begin();

 if(!storageOk||!journalOk||hp::RecoveryManager::instance().required()){
  enterRecovery(!storageOk?"systemdata_mount":!journalOk?"journal_init":nullptr);
  return;
 }
 if(!assetsOk)hp::EventLog::instance().add("WARN","ASSET_PARTITION","SystemData/AppFS partition not fully available");

 bool core=settingsOk&&powerOk&&displayPerformanceOk&&networkWorkerOk&&ethernetOk&&networkOk;
 core&=hp::TimeService::instance().begin();
 core&=hp::AudioHardwareService::instance().begin();
 core&=hp::AmbientLightService::instance().begin();
 core&=hp::BluetoothService::instance().begin();
 core&=hp::NasService::instance().begin();
 if(!hp::UsbStorageService::instance().begin())hp::EventLog::instance().add("WARN","USB_STORAGE","USB host unavailable");
 // Port 8088 is intentionally OFF during normal operation.
 // Port 80 can start it on demand for at most 30 minutes.
 core&=hp::C6UpdateService::instance().begin();
 core&=hp::CalendarSyncService::instance().begin();
 hp::IdleService::instance().begin();
 hp::OtaService::instance().begin();
 hp::LanguageService::instance().loadExternalAtBoot();
 const bool backendsOk=hp::beginSharedAppServices();
 const bool appsOk=registerApps();
 const bool jobsOk=registerJobs();
 const bool uiOk=hp::UiShell::instance().begin();
 hp::BootSplash::hide();
 if(uiOk){hp::DisplayUiGuard guard(1000);if(guard)hp::UiShell::instance().installControlCenterThemeControls();}
 // Start the user-friendly management UI only after its app/system backends
 // have their initial snapshots. It uses port 80 and does not duplicate them.
 const bool webOk=hp::WebServerService::instance().begin();
 Serial.printf("[BOOT] internal_heap_free=%u largest=%u\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
 if(!(core&&backendsOk&&appsOk&&jobsOk&&uiOk&&webOk)){hp::EventLog::instance().add("ERROR","DEGRADED_BOOT","One or more required local services failed");hp::BootManager::instance().markFailed("required_service_init");}
}

void loop(){
 if(recoveryMode){
  // Keep the minimum network stack alive while the normal app/UI services are
  // intentionally not started. RecoveryWebService itself is event driven.
  hp::NetworkService::instance().loop();
  hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Ui);
  hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Storage);
  hp::SystemSupervisor::instance().loop();
  {hp::DisplayUiGuard guard(100);if(guard)hp::SecurityUi::sync();}
  hp::DisplayManager::instance().serviceLvgl();
  delay(20);return;
 }
 hp::CalendarSyncService::instance().backgroundTick();
 hp::CalendarReminderService::instance().tick();
 hp::FileTransferService::instance().loop();
 hp::NetworkService::instance().loop();hp::TimeService::instance().loop();hp::IdleService::instance().loop();hp::PowerManager::instance().loop();hp::AmbientLightService::instance().loop();hp::UiShell::instance().loop();hp::HomeReorderController::instance().loop();
 hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Storage);hp::SystemSupervisor::instance().heartbeat(hp::ServiceId::Media);hp::SystemSupervisor::instance().loop();
 if(!bootCommitted&&millis()-lastHealth>=1000){lastHealth=millis();const auto report=hp::BootHealthCheck::instance().evaluate();if(report.ok()){if(hp::OtaService::instance().commitIfHealthy(report)){hp::BootManager::instance().markHealthy();bootCommitted=true;hp::EventLog::instance().add("INFO","BOOT_HEALTH","All mandatory local health checks passed");}}}
 switch(hp::IdleService::instance().stage()){case hp::IdleStage::Active:delay(10);break;case hp::IdleStage::Dimmed:delay(20);break;case hp::IdleStage::Saver:delay(50);break;case hp::IdleStage::Off:delay(100);break;}
}
extern "C" void app_main(){initArduino();setup();for(;;)loop();}
