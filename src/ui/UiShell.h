#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../services/IdleService.h"
#include "ControlPull.h"
#include "ThemeControls.h"
#include "../services/DockShortcutService.h"

namespace hp {

class UiShell {
public:
    static UiShell& instance();

    bool begin();
    void loop();
    lv_obj_t* content() const { return content_; }
    lv_obj_t* homeContent() const { return home_; }
    uint32_t homeRevision()const{return homeRevision_;}
    void showHome();
    void hideHomeBackground(lv_obj_t* host);
    void setAppFullscreen(lv_obj_t* host,bool enabled); // Existing UI lock required.
    void appsChanged();
    void requestAppsRefresh();
    void requestDockRefresh();
    void setDesktopEditing(bool editing); // Existing LVGL lock required.
    bool desktopEditing() const { return desktopEditing_; }
    uint8_t desktopPage() const { return homePage_; }
    void requestDesktopPage(int8_t direction) { pendingHomePageChange_.store(direction); }
    lv_obj_t* dock() const { return dock_; } // UI lock required.
    bool requestLauncherOpen(const char* id);
    bool requestAppSettings(const char* id);
    bool requestAppOpen(const char* id);
    bool requestAppOpen(const char* id,const char* arguments);
    void refreshLauncherAfterInstall();
    void refreshTheme();
    // Explicit ownership: UiShell owns the Control Center UI. UiTheme only
    // provides colors/styles and never discovers or creates controls itself.
    void installControlCenterThemeControls() {
        ThemeControls::instance().buildControlCenter(controlCenter_);
    }
    uint32_t frameCount() const { return frameCount_; }
    uint32_t maxFrameUs() const { return maxFrameUs_; }
    uint16_t fps() const { return fps_; }
    enum class Phase:uint8_t{Resources,Navigation,Callbacks,Housekeeping};
    uint32_t maxPhaseUs(Phase phase)const{return phaseMaxUs_[static_cast<unsigned>(phase)].load();}

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* wallpaperLayer_ = nullptr;
    lv_obj_t* top_ = nullptr;
    lv_obj_t* edge_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* home_ = nullptr;
    lv_obj_t* dock_ = nullptr;
    bool desktopEditing_=false;
    uint32_t suppressLauncherUntil_=0;
    lv_obj_t* editDone_=nullptr;
    lv_obj_t* dockRemove_[DockShortcutService::kMaxItems]{};
    size_t dockRemoveCount_=0;
    std::atomic<bool> dockDirty_{true};
    char dockIds_[DockShortcutService::kMaxItems][DockShortcutService::kIdLength]{};
    std::atomic<bool> homeDirty_{true};
    uint32_t homeRevision_=0;
    char homeTileIds_[10][40]{};
    lv_obj_t* saver_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* statusNetwork_ = nullptr;
    lv_obj_t* statusSd_ = nullptr;
    lv_obj_t* media_ = nullptr;
    lv_obj_t* homeBtn_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* appSettingsBack_ = nullptr;
    std::atomic<int8_t> pendingHomePageChange_{0};
    lv_obj_t* controlCenter_ = nullptr;
    ControlPull controlPull_;
    uint32_t lastControlDragUpdate_=0;
    uint32_t lastWakeTap_ = 0;

    uint32_t lastClock_ = 0;
    uint32_t lastIdleApply_ = 0;
    int lastAppliedBrightness_ = -1;
    IdleStage appliedIdleStage_ = IdleStage::Off;
    uint32_t frameCount_ = 0, fpsWindowStart_ = 0, fpsWindowFrames_ = 0, maxFrameUs_ = 0;
    uint16_t fps_ = 0;
    std::atomic<uint32_t> phaseMaxUs_[4]{};
    void recordPhase(Phase phase,uint32_t elapsed){auto& value=phaseMaxUs_[static_cast<unsigned>(phase)];auto old=value.load();while(elapsed>old&&!value.compare_exchange_weak(old,elapsed)){} }
    uint8_t homePage_ = 0;
    bool wallpaperRefreshPending_ = false;
    std::atomic<bool> appsRefreshPending_{false};
    static constexpr size_t APP_ID_CAPACITY = 320;
    QueueHandle_t pendingAppQueue_ = nullptr;

    static void appBtn(lv_event_t* e);
    static void launcherBtn(lv_event_t* e);
    static void dockRemoveBtn(lv_event_t* e);
    static void desktopEditEvent(lv_event_t* e);
    static void homeBtn(lv_event_t* e);
    static void saverBtn(lv_event_t* e);
    static void touchActivity(lv_event_t* e);
    static void mediaBtn(lv_event_t* e);
    static void controlDrag(lv_event_t* e);
    static void controlAnimationDone(lv_anim_t* animation);
    static void controlBrightness(lv_event_t* e);
    static void controlWifi(lv_event_t* e);
    static void controlEnergy(lv_event_t* e);
    static void controlClose(lv_event_t* e);
    static void homePageBtn(lv_event_t* e);
    static void homeGesture(lv_event_t* e);
    void changeHomePage(int change);

    void rebuildHome();
    void rebuildDock();
    void openAppsSettingsList();
    void buildSaver();
    void buildControlCenter();
    void settleControlCenter(bool open);
    void updateStatus();
    void applyIdle();
};

}
