#pragma once
#include "../../ui/IconCatalog.h"
#include "../../core/AppManager.h"
#include "../../services/NetworkService.h"
#include "../../services/SdFormatConfirmation.h"
#include "../calendar/CalendarApp.h"
namespace hp {
class SettingsApp : public IApp {
public:
 const char* id()const override{return "settings";}
 const char* name()const override{return "Einstellungen";}
 const char* icon()const override{return "@settings";}
 bool launcherVisible()const override{return true;}
 bool begin()override{return true;}
 void open(lv_obj_t*)override;
 void setLaunchArguments(const String&)override;
 void selectAppsPage(){page_=Page::Apps;}
 void close()override{
   usersView_.close();
   sdFormatConfirmation_.reset();
   if(scanTimer_){lv_timer_delete(scanTimer_);scanTimer_=nullptr;}
   if(bleTimer_){lv_timer_delete(bleTimer_);bleTimer_=nullptr;}
   if(nasTimer_){lv_timer_delete(nasTimer_);nasTimer_=nullptr;}
   if(scrollSettleTimer_){lv_timer_delete(scrollSettleTimer_);scrollSettleTimer_=nullptr;}
   deferredWifiRefresh_=deferredBleRefresh_=deferredNasRefresh_=false;
   scrollBusyUntil_=0;keyboardPressTarget_=nullptr;keyboardPressValid_=false;
   root_=nullptr;keyboard_=nullptr;wifiNetworksList_=nullptr;keyboardNumeric_=false;
   timeDay_=timeMonth_=timeYear_=timeHour_=timeMinute_=timeManualStatus_=nullptr;
   pagePanel_=nullptr;sidebar_=nullptr;lastRenderedPage_=255;
   for(auto& button:navButtons_) button=nullptr;
 }
private:
 enum class Page:uint8_t{Display,Energy,Network,Bluetooth,Sound,Storage,Nas,Apps,System,Updates,Diagnostics,Security,Email,Users};
 CalendarApp usersView_;
 static void categoryCb(lv_event_t*);static void energyChanged(lv_event_t*);static void wakeTapChanged(lv_event_t*);
 static void sdFormatCb(lv_event_t*);static void sdFormatCancelCb(lv_event_t*);
 SdFormatConfirmation sdFormatConfirmation_;
 static void wifiScheduleChanged(lv_event_t*);
 static void idleProfileChanged(lv_event_t*);
 static void brightnessChanged(lv_event_t*);static void brightnessSliderChanged(lv_event_t*);static void audioVolumeChanged(lv_event_t*);
 static void volumeSliderChanged(lv_event_t*);static void wifiEcoChanged(lv_event_t*);static void wifiSetupCb(lv_event_t*);
 static void wifiProfileCb(lv_event_t*);
 static void wifiOverviewPreferCb(lv_event_t*);
 static void wifiPreferCb(lv_event_t*);static void wifiForgetCb(lv_event_t*);
 static void wifiScanCb(lv_event_t*);static void wifiScanPollCb(lv_timer_t*);static void wifiNetworkCb(lv_event_t*);
 static void wifiScanRefreshCb(void*);static void wifiFormScrollCb(lv_event_t*);
 void renderWifiNetworkList();
 static void wifiConnectCb(lv_event_t*);static void wifiEnabledCb(lv_event_t*);static void wifiPasswordVisibilityCb(lv_event_t*);static void wifiQrCb(lv_event_t*);
 static void bleEnabledCb(lv_event_t*);static void bleScanCb(lv_event_t*);static void blePollCb(lv_timer_t*);
 static void nasSetupCb(lv_event_t*);static void nasSaveCb(lv_event_t*);static void c6UpdateCb(lv_event_t*);
 static void mediaP4UpdateCb(lv_event_t*);static void mediaC6UpdateCb(lv_event_t*);static void mediaRefreshCb(lv_event_t*);static void mediaRebootCb(lv_event_t*);
 bool mediaRebootConfirm_=false;uint8_t mediaInstallConfirm_=0;
 static void nasOpenCb(lv_event_t*);static void nasBackCb(lv_event_t*);
 static void nasFileCb(lv_event_t*);static void nasPollCb(lv_timer_t*);
 static void manualTimeSetCb(lv_event_t*);
 static void scrollPerfCb(lv_event_t*);static void scrollSettleCb(lv_timer_t*);
 static void textareaFocusCb(lv_event_t*);static void keyboardEventCb(lv_event_t*);
 void rebuild();void buildWifiSetup();void buildNasSetup();void buildNasBrowser();
 void createKeyboard();void showKeyboard(lv_obj_t*);void hideKeyboard();
 void configureScrollSurface(lv_obj_t*);
 bool scrollBusy() const;
 void requestDeferredRefresh(bool wifi=false,bool ble=false,bool nas=false);
 lv_obj_t* sidebar_=nullptr;lv_obj_t* pagePanel_=nullptr;
 lv_obj_t* navButtons_[14] = {};
 uint8_t lastRenderedPage_=255;
 lv_obj_t* root_=nullptr;lv_obj_t* energySwitch_=nullptr;lv_obj_t* wakeTapSwitch_=nullptr;lv_obj_t* brightnessSlider_=nullptr;
 lv_obj_t* idleActiveBrightness_=nullptr;lv_obj_t* idleActiveValue_=nullptr;
 lv_obj_t* idleDimAfter_=nullptr;lv_obj_t* idleDimAfterValue_=nullptr;lv_obj_t* idleDimBrightness_=nullptr;lv_obj_t* idleDimBrightnessValue_=nullptr;
 lv_obj_t* idleSaverAfter_=nullptr;lv_obj_t* idleSaverAfterValue_=nullptr;lv_obj_t* idleSaverBrightness_=nullptr;lv_obj_t* idleSaverBrightnessValue_=nullptr;
 lv_obj_t* idleOffAfter_=nullptr;lv_obj_t* idleOffAfterValue_=nullptr;lv_obj_t* alwaysOnSwitch_=nullptr;lv_obj_t* alwaysOnBrightness_=nullptr;lv_obj_t* alwaysOnBrightnessValue_=nullptr;
 lv_obj_t* wifiScheduleSwitch_=nullptr;lv_obj_t* wifiScheduleStartHour_=nullptr;lv_obj_t* wifiScheduleStartMinute_=nullptr;
 lv_obj_t* wifiScheduleEndHour_=nullptr;lv_obj_t* wifiScheduleEndMinute_=nullptr;
 lv_obj_t* wifiPowerSaveSwitch_=nullptr;lv_obj_t* wifiOffOnLanSwitch_=nullptr;lv_obj_t* wifiSsid_=nullptr;lv_obj_t* wifiPassword_=nullptr;
 lv_obj_t* wifiIp_=nullptr;lv_obj_t* wifiGateway_=nullptr;lv_obj_t* wifiSubnet_=nullptr;lv_obj_t* wifiDns1_=nullptr;lv_obj_t* wifiDns2_=nullptr;
 lv_obj_t* wifiPasswordVisibility_=nullptr;lv_obj_t* keyboard_=nullptr;lv_obj_t* wifiStatus_=nullptr;lv_obj_t* wifiNetworksList_=nullptr;lv_obj_t* nasName_=nullptr;
 lv_obj_t* nasHost_=nullptr;lv_obj_t* nasPort_=nullptr;lv_obj_t* nasPath_=nullptr;lv_obj_t* nasUsername_=nullptr;lv_obj_t* nasPassword_=nullptr;
 lv_obj_t* nasProtocol_=nullptr;
 lv_obj_t* nasStatus_=nullptr;lv_obj_t* bleStatus_=nullptr;lv_obj_t* bleList_=nullptr;lv_obj_t* bleEnabledSwitch_=nullptr;
 lv_obj_t* timeDay_=nullptr;lv_obj_t* timeMonth_=nullptr;lv_obj_t* timeYear_=nullptr;
 lv_obj_t* timeHour_=nullptr;lv_obj_t* timeMinute_=nullptr;lv_obj_t* timeManualStatus_=nullptr;
 lv_timer_t* scanTimer_=nullptr;lv_timer_t* bleTimer_=nullptr;lv_timer_t* nasTimer_=nullptr;
 String nasCurrentFolder_;
 uint32_t nasSeenGeneration_=0;
 uint8_t nasSelectedProfile_=0;
 bool nasBrowse_=false;uint32_t scanStartedAt_=0;WifiNetwork scanResults_[12];uint8_t scanCount_=0;
 uint8_t wifiProfileIndex_=0;
 int8_t wifiDeleteConfirmSlot_=-1;uint32_t wifiDeleteConfirmAt_=0;
 lv_obj_t* keyboardPressTarget_=nullptr;
 lv_point_t keyboardPressPoint_{};
 bool keyboardPressValid_=false;uint32_t wifiScrollUntil_=0;
 uint32_t scrollBusyUntil_=0;
 bool deferredWifiRefresh_=false;bool deferredBleRefresh_=false;bool deferredNasRefresh_=false;
 lv_timer_t* scrollSettleTimer_=nullptr;
 bool wifiSetup_=false;bool nasSetup_=false;bool keyboardNumeric_=false;Page page_=Page::Display;
};
}
