#include "UiShell.h"
#include "FirmwareUpdateOverlay.h"
#include "../services/PhotoLibraryService.h"
#include "../services/LanguageService.h"
#include "../services/IntercomService.h"
#include "../apps/calendar/CalendarReminderService.h"
#include "../services/VideoService.h"
#include "../apps/lua/LuaIntercomBridge.h"
#include "HomeReorderController.h"
#include "../services/DockShortcutService.h"
#include "../services/UiPerformanceMonitor.h"
#include <ArduinoJson.h>
#include "../services/AppIdleActionService.h"
#include "../apps/lua/LuaMediaStorageBridge.h"
#include "SecurityUi.h"
#include "../services/SecurityService.h"
#include "../apps/settings/SettingsApp.h"
#include <cstring>
#include "UiTheme.h"
#include "IconCatalog.h"
#include "NativeAppIcon.h"
#include "WallpaperService.h"

#include "../core/AppManager.h"
#include "../core/AppBackgroundScheduler.h"
#include "../core/AppRegistry.h"
#include "../core/EventLog.h"
#include "../services/IdleService.h"
#include "../services/TimeService.h"
#include "../services/MediaSessionService.h"
#include "../services/NetworkService.h"
#include "../services/StorageService.h"
#include "../hardware/DisplayManager.h"
#include "../core/SystemSupervisor.h"
#include "../core/BootHealthCheck.h"
#include "../services/SettingsService.h"
#include "../services/AmbientLightService.h"
#include "../services/PowerManager.h"
#include "../apps/browser/WebAppManager.h"
#include "../apps/browser/BrowserApp.h"
#include "../apps/lua/LuaAppRegistry.h"
#include "../apps/lua/LuaApp.h"
#include "../apps/lua/LuaEvents.h"
#include "../apps/lua/LuaPackageInstaller.h"
#include "../apps/settings/DynamicAppSettings.h"

namespace hp {

static void setHiddenIfChanged(lv_obj_t* obj, bool hidden);
static bool validQueuedAppId(const char* id) {
    if(!id||!id[0]||strlen(id)>31)return false;
    for(const char* p=id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return false;
    return true;
}

UiShell& UiShell::instance() { static UiShell u; return u; }

static lv_obj_t* makeSymbolButton(lv_obj_t* parent,const char* symbol,int w,int h,lv_event_cb_t cb,void* userData=nullptr){
    auto* b=lv_button_create(parent); lv_obj_set_size(b,w,h); UiTheme::instance().styleRoundIcon(b);
    auto* l=createAppIcon(b,symbol,32,UiTheme::instance().textPrimary());lv_obj_center(l);
    if(cb)lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,userData); return b;
}

bool UiShell::begin(){
    DisplayUiGuard uiGuard(5000); if(!uiGuard)return false;
    LuaApp::bindBackgroundUiTask();
    if (!pendingAppQueue_) {
        pendingAppQueue_ = xQueueCreate(1, APP_ID_CAPACITY);
        if (!pendingAppQueue_) {
            EventLog::instance().add("ERROR", "UI_APP_QUEUE", "App-Start-Queue konnte nicht erstellt werden");
            return false;
        }
    }
    UiTheme::instance().begin(); WallpaperService::instance().begin();
    root_=lv_obj_create(lv_screen_active()); lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));
    lv_obj_set_style_pad_all(root_,0,0); lv_obj_clear_flag(root_,LV_OBJ_FLAG_SCROLLABLE); UiTheme::instance().styleScreen(root_);
    wallpaperLayer_=lv_obj_create(root_); lv_obj_set_size(wallpaperLayer_,LV_PCT(100),LV_PCT(100)); lv_obj_set_pos(wallpaperLayer_,0,0);
    lv_obj_set_style_pad_all(wallpaperLayer_,0,0); lv_obj_set_style_border_width(wallpaperLayer_,0,0);
    lv_obj_clear_flag(wallpaperLayer_,LV_OBJ_FLAG_SCROLLABLE); WallpaperService::instance().attach(wallpaperLayer_);
    top_=lv_obj_create(root_); lv_obj_set_size(top_,LV_PCT(100),76); lv_obj_align(top_,LV_ALIGN_TOP_MID,0,0);
    lv_obj_set_style_pad_left(top_,18,0); lv_obj_set_style_pad_right(top_,20,0); UiTheme::instance().styleTopBar(top_);
    homeBtn_=makeSymbolButton(top_,IconCatalog::home(),48,48,homeBtn); lv_obj_add_flag(homeBtn_,LV_OBJ_FLAG_GESTURE_BUBBLE); lv_obj_align(homeBtn_,LV_ALIGN_LEFT_MID,0,0);
    auto* title=lv_label_create(top_); lv_label_set_text(title,"Home-Display");
    lv_obj_set_style_text_font(title,fontWithGermanFallback(&lv_font_montserrat_28),0); lv_obj_set_style_text_color(title,UiTheme::instance().textPrimary(),0);
    lv_obj_align(title,LV_ALIGN_LEFT_MID,68,-1); lv_obj_add_flag(title,LV_OBJ_FLAG_GESTURE_BUBBLE);
    auto* subtitle=lv_label_create(top_);subtitle_=subtitle; lv_label_set_text(subtitle,"Von oben nach unten wischen: Kontrollzentrum");
    lv_obj_set_style_text_font(subtitle,fontWithGermanFallback(&lv_font_montserrat_14),0); lv_obj_set_style_text_color(subtitle,UiTheme::instance().textSecondary(),0);
    lv_obj_align(subtitle,LV_ALIGN_LEFT_MID,70,24); lv_obj_add_flag(subtitle,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(top_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(top_,LV_OBJ_FLAG_PRESS_LOCK);lv_obj_add_event_cb(top_,controlDrag,LV_EVENT_ALL,nullptr);
    statusNetwork_=lv_label_create(top_); lv_obj_set_style_text_font(statusNetwork_,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_set_style_text_color(statusNetwork_,UiTheme::instance().textSecondary(),0); lv_obj_align(statusNetwork_,LV_ALIGN_RIGHT_MID,-190,0); lv_obj_add_flag(statusNetwork_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    statusSd_=lv_label_create(top_); lv_obj_set_style_text_font(statusSd_,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_set_style_text_color(statusSd_,UiTheme::instance().textSecondary(),0); lv_obj_align(statusSd_,LV_ALIGN_RIGHT_MID,-145,0); lv_obj_add_flag(statusSd_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    date_=lv_label_create(top_); lv_obj_set_style_text_font(date_,fontWithGermanFallback(&lv_font_montserrat_14),0);
    lv_obj_set_style_text_color(date_,UiTheme::instance().textSecondary(),0); lv_obj_align(date_,LV_ALIGN_RIGHT_MID,-72,18); lv_obj_add_flag(date_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    clock_=lv_label_create(top_); lv_label_set_text(clock_,"--:--"); lv_obj_set_style_text_font(clock_,fontWithGermanFallback(&lv_font_montserrat_24),0);
    lv_obj_set_style_text_color(clock_,UiTheme::instance().textPrimary(),0); lv_obj_align(clock_,LV_ALIGN_RIGHT_MID,0,-8); lv_obj_add_flag(clock_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    appSettingsBack_=makeSymbolButton(top_,LV_SYMBOL_LEFT " Einstellungen",220,44,[](lv_event_t*){UiShell::instance().requestAppSettings(nullptr);});
    lv_obj_align(appSettingsBack_,LV_ALIGN_CENTER,50,0);setHiddenIfChanged(appSettingsBack_,true);
    content_=lv_obj_create(root_); lv_obj_set_size(content_,LV_PCT(100),724); lv_obj_set_pos(content_,0,76); lv_obj_set_style_pad_all(content_,26,0);
    lv_obj_set_style_bg_opa(content_,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(content_,0,0); lv_obj_clear_flag(content_,LV_OBJ_FLAG_SCROLLABLE);
    home_=lv_obj_create(root_);lv_obj_set_size(home_,LV_PCT(100),724);lv_obj_set_pos(home_,0,76);
    lv_obj_set_style_bg_opa(home_,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(home_,0,0);
    lv_obj_clear_flag(home_,LV_OBJ_FLAG_SCROLLABLE);setHiddenIfChanged(content_,true);
    buildSaver(); buildControlCenter(); rebuildHome();
    auto* edge=lv_obj_create(root_);lv_obj_remove_style_all(edge);lv_obj_set_size(edge,LV_PCT(100),24);lv_obj_set_pos(edge,0,0);
    edge_=edge;
    lv_obj_clear_flag(edge,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(edge,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(edge,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(edge,controlDrag,LV_EVENT_ALL,nullptr);
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(indev, touchActivity, LV_EVENT_PRESSED, this);
            lv_indev_add_event_cb(indev, touchActivity, LV_EVENT_PRESSING, this);
            lv_indev_add_event_cb(indev, touchActivity, LV_EVENT_RELEASED, this);
        }
    }
    if (LuaAppRegistry::instance().requestBackgroundScan())
        appsRefreshPending_.store(true, std::memory_order_release);
    beginFirmwareUpdateOverlay();
    BootHealthCheck::instance().reportUiReady();
    SecurityUi::sync();
    BootHealthCheck::instance().reportTouchReady(DisplayManager::instance().touchReady()); updateStatus(); return true;
}

void UiShell::buildSaver(){
    saver_=lv_obj_create(root_); lv_obj_set_size(saver_,LV_PCT(100),LV_PCT(100)); lv_obj_set_style_bg_color(saver_,lv_color_hex(0x000000),0);
    lv_obj_set_style_bg_opa(saver_,LV_OPA_COVER,0); lv_obj_set_style_border_width(saver_,0,0); setHiddenIfChanged(saver_, true);
    lv_obj_add_flag(saver_,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(saver_,saverBtn,LV_EVENT_PRESSED,nullptr);
    auto* time=lv_label_create(saver_); lv_label_set_text(time,"--:--"); lv_obj_set_style_text_font(time,fontWithGermanFallback(&lv_font_montserrat_48),0);
    lv_obj_set_style_text_color(time,UiTheme::instance().textPrimary(),0); lv_obj_align(time,LV_ALIGN_CENTER,0,-130);
    auto* date=lv_label_create(saver_); lv_label_set_text(date,""); lv_obj_set_style_text_font(date,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_set_style_text_color(date,UiTheme::instance().textSecondary(),0); lv_obj_align(date,LV_ALIGN_CENTER,0,-78);
    auto* mediaCard=lv_obj_create(saver_); lv_obj_set_size(mediaCard,720,230); lv_obj_align(mediaCard,LV_ALIGN_CENTER,0,70); UiTheme::instance().styleGlassPanel(mediaCard);
    media_=lv_label_create(mediaCard); lv_label_set_text(media_,""); lv_obj_set_width(media_,640); lv_obj_set_style_text_align(media_,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(media_,fontWithGermanFallback(&lv_font_montserrat_18),0); lv_obj_set_style_text_color(media_,UiTheme::instance().textPrimary(),0);
    lv_obj_align(media_,LV_ALIGN_TOP_MID,0,20);
    const char* symbols[]={LV_SYMBOL_PREV,LV_SYMBOL_PLAY,LV_SYMBOL_NEXT,LV_SYMBOL_VOLUME_MID,LV_SYMBOL_VOLUME_MAX};
    MediaCommand commands[]={MediaCommand::Previous,MediaCommand::PlayPause,MediaCommand::Next,MediaCommand::VolumeDown,MediaCommand::VolumeUp};
    for(int i=0;i<5;++i){auto* b=makeSymbolButton(mediaCard,symbols[i],i==1?76:62,i==1?76:62,mediaBtn,(void*)(uintptr_t)commands[i]);
        lv_obj_align(b,LV_ALIGN_BOTTOM_MID,(i-2)*92,-18);}
}

void UiShell::buildControlCenter(){
    controlCenter_=lv_obj_create(root_); lv_obj_set_size(controlCenter_,LV_PCT(100),LV_PCT(100)); lv_obj_align(controlCenter_,LV_ALIGN_TOP_LEFT,0,0);
    UiTheme::instance().styleScreen(controlCenter_);lv_obj_set_style_radius(controlCenter_,0,0);lv_obj_set_style_border_width(controlCenter_,0,0);
    lv_obj_clear_flag(controlCenter_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(controlCenter_,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(controlCenter_,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(controlCenter_,controlDrag,LV_EVENT_ALL,nullptr); lv_obj_add_flag(controlCenter_,LV_OBJ_FLAG_HIDDEN);
    auto* handle=lv_obj_create(controlCenter_);lv_obj_remove_style_all(handle);lv_obj_set_size(handle,LV_PCT(100),64);lv_obj_align(handle,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_flag(handle,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(handle,LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(handle,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(handle,controlDrag,LV_EVENT_ALL,nullptr);
    auto* pill=lv_obj_create(handle);lv_obj_remove_style_all(pill);lv_obj_set_size(pill,140,6);lv_obj_center(pill);lv_obj_set_style_radius(pill,3,0);
    lv_obj_clear_flag(pill,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(pill,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pill,UiTheme::instance().textSecondary(),0);lv_obj_set_style_bg_opa(pill,LV_OPA_COVER,0);
    auto* title=lv_label_create(controlCenter_); lv_label_set_text(title,"Kontrollzentrum"); lv_obj_set_style_text_font(title,fontWithGermanFallback(&lv_font_montserrat_28),0);
    lv_obj_set_style_text_color(title,UiTheme::instance().textPrimary(),0); lv_obj_align(title,LV_ALIGN_TOP_LEFT,70,58);
    auto* close=makeSymbolButton(controlCenter_,LV_SYMBOL_CLOSE,64,64,controlClose); lv_obj_align(close,LV_ALIGN_TOP_RIGHT,-56,48);
    auto* brightness=lv_label_create(controlCenter_); lv_label_set_text(brightness,LV_SYMBOL_EYE_OPEN "  Helligkeit");
    lv_obj_set_style_text_font(brightness,fontWithGermanFallback(&lv_font_montserrat_18),0); lv_obj_align(brightness,LV_ALIGN_TOP_LEFT,120,165);
    auto* brightnessSlider=lv_slider_create(controlCenter_); lv_obj_set_size(brightnessSlider,1040,40); lv_obj_align(brightnessSlider,LV_ALIGN_TOP_LEFT,120,205);
    lv_slider_set_range(brightnessSlider,15,255); lv_slider_set_value(brightnessSlider,SettingsService::instance().snapshot().brightness,LV_ANIM_OFF);
    lv_obj_add_event_cb(brightnessSlider,controlBrightness,LV_EVENT_VALUE_CHANGED,nullptr); lv_obj_add_event_cb(brightnessSlider,controlBrightness,LV_EVENT_RELEASED,nullptr);
    auto* wifiLabel=lv_label_create(controlCenter_); lv_label_set_text(wifiLabel,LV_SYMBOL_WIFI "  WLAN");
    lv_obj_set_style_text_font(wifiLabel,fontWithGermanFallback(&lv_font_montserrat_18),0); lv_obj_align(wifiLabel,LV_ALIGN_TOP_LEFT,120,315);
    auto* wifi=lv_switch_create(controlCenter_); lv_obj_align(wifi,LV_ALIGN_TOP_LEFT,480,305); if(NetworkService::instance().wifiEnabled())lv_obj_add_state(wifi,LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifi,controlWifi,LV_EVENT_VALUE_CHANGED,nullptr);
    auto* energyLabel=lv_label_create(controlCenter_); lv_label_set_text(energyLabel,LV_SYMBOL_CHARGE "  Energiesparen");
    lv_obj_set_style_text_font(energyLabel,fontWithGermanFallback(&lv_font_montserrat_18),0); lv_obj_align(energyLabel,LV_ALIGN_TOP_LEFT,120,405);
    auto* energy=lv_switch_create(controlCenter_); lv_obj_align(energy,LV_ALIGN_TOP_LEFT,480,395); if(SettingsService::instance().snapshot().powerManagement.energySaving)lv_obj_add_state(energy,LV_STATE_CHECKED);
    lv_obj_add_event_cb(energy,controlEnergy,LV_EVENT_VALUE_CHANGED,nullptr);
    auto* ble=lv_label_create(controlCenter_); lv_label_set_text(ble,LV_SYMBOL_WARNING "  Bluetooth: noch nicht verfuegbar");
    lv_obj_set_style_text_font(ble,fontWithGermanFallback(&lv_font_montserrat_18),0); lv_obj_set_style_text_color(ble,UiTheme::instance().textSecondary(),0);
    lv_obj_align(ble,LV_ALIGN_TOP_LEFT,120,510);
}

void UiShell::controlAnimationDone(lv_anim_t* a){
    auto* panel=static_cast<lv_obj_t*>(a->var);
    if(!panel)return;
    const int height=lv_display_get_vertical_resolution(nullptr);
    // set_y changes a style; LVGL can still report the previous rendered
    // coordinates here. Never infer the snap destination from deferred layout.
    if(a->end_value<0){
        lv_obj_set_y(panel,-height);
        lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    }else{
        lv_obj_set_y(panel,0);
        lv_obj_remove_flag(panel,LV_OBJ_FLAG_HIDDEN);
    }
}

void UiShell::settleControlCenter(bool open){
    if(!controlCenter_)return;

    IdleService::instance().activity();
    lv_anim_delete(controlCenter_,nullptr);

    const int height=lv_display_get_vertical_resolution(nullptr);
    const bool hidden=lv_obj_has_flag(controlCenter_,LV_OBJ_FLAG_HIDDEN);

    if(hidden&&!open){
        lv_obj_set_y(controlCenter_,-height);
        return;
    }

    if(hidden) {
        ThemeControls::instance().refreshControlCenter();
        lv_obj_set_y(controlCenter_,-height);
    }
    lv_obj_remove_flag(controlCenter_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(controlCenter_);

    const int target=open?0:-height;
    const int current=std::max(-height,std::min(0,static_cast<int>(lv_obj_get_y_aligned(controlCenter_))));
    lv_obj_set_y(controlCenter_,current);

    if(current==target){
        if(!open)lv_obj_add_flag(controlCenter_,LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a,controlCenter_);
    lv_anim_set_values(&a,current,target);

    // Distance-proportional snap: short remaining distances don't feel sluggish.
    const int distance=std::abs(target-current);
    const uint32_t duration=static_cast<uint32_t>(
        std::max(90,std::min(180,90+(distance*90)/std::max(1,height)))
    );
    lv_anim_set_duration(&a,duration);
    lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a,[](void* obj,int32_t y){
        lv_obj_set_y(static_cast<lv_obj_t*>(obj),y);
    });
    lv_anim_set_completed_cb(&a,controlAnimationDone);
    lv_anim_start(&a);
}

void UiShell::controlDrag(lv_event_t* e){
    auto& s=UiShell::instance();
    auto* indev=lv_indev_active();
    if(!indev||!s.controlCenter_)return;

    const auto code=lv_event_get_code(e);
    const auto idleStage=IdleService::instance().stage();

    // A real touch during Dim/Saver is activity immediately, before checking
    // whether the saver layer is still visually present from the previous UI pass.
    if(code==LV_EVENT_PRESSED && idleStage!=IdleStage::Off){
        IdleService::instance().activity();
        if(s.saver_)setHiddenIfChanged(s.saver_,true);
    }

    if(SecurityService::instance().tabletLocked()||
       idleStage==IdleStage::Off||
       (s.saver_&&!lv_obj_has_flag(s.saver_,LV_OBJ_FLAG_HIDDEN))){
        s.controlPull_.active=false;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev,&point);
    const uint32_t now=lv_tick_get();
    const int height=lv_display_get_vertical_resolution(nullptr);

    if(code==LV_EVENT_PRESSED){
        IdleService::instance().activity();
        lv_anim_delete(s.controlCenter_,nullptr);
        const bool hidden=lv_obj_has_flag(s.controlCenter_,LV_OBJ_FLAG_HIDDEN);
        const int visible=hidden?0:std::max(0,std::min(height,height+static_cast<int>(lv_obj_get_y_aligned(s.controlCenter_))));
        s.controlPull_.begin(point.x,point.y,visible,height,now);
        s.lastControlDragUpdate_=now;
    }else if(code==LV_EVENT_PRESSING){
        // Holding/dragging the sheet is continuous user activity even if the
        // visual 40 Hz movement limiter suppresses this particular frame.
        IdleService::instance().activity();

        if(s.controlPull_.move(point.x,point.y,now)){
            if(lv_obj_has_flag(s.controlCenter_,LV_OBJ_FLAG_HIDDEN)){
                ThemeControls::instance().refreshControlCenter();
                lv_obj_remove_flag(s.controlCenter_,LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s.controlCenter_);
            }

            const int y=s.controlPull_.amount-height;
            const int delta=y-lv_obj_get_y_aligned(s.controlCenter_);
            if(now-s.lastControlDragUpdate_>=25 && (delta>2||delta<-2)){
                lv_obj_set_y(s.controlCenter_,y);
                s.lastControlDragUpdate_=now;
            }
        }
    }else if((code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)&&s.controlPull_.active){
        IdleService::instance().activity();

        // Important: sync the actually drawn panel to the final gesture amount
        // before starting the snap animation. The 40 Hz limiter can otherwise
        // leave the rendered Y position one or more samples behind.
        if(s.controlPull_.claimed){
            if(lv_obj_has_flag(s.controlCenter_,LV_OBJ_FLAG_HIDDEN)){
                lv_obj_remove_flag(s.controlCenter_,LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s.controlCenter_);
            }
            lv_obj_set_y(s.controlCenter_,s.controlPull_.amount-height);
        }

        const bool open=s.controlPull_.finish(now);
        s.settleControlCenter(open);
    }
}

void UiShell::controlBrightness(lv_event_t* e){auto* slider=static_cast<lv_obj_t*>(lv_event_get_target(e));uint8_t v=(uint8_t)lv_slider_get_value(slider);DisplayManager::instance().setBrightness(v);if(lv_event_get_code(e)==LV_EVENT_RELEASED)SettingsService::instance().setBrightness(v);}
void UiShell::controlWifi(lv_event_t* e){auto* sw=static_cast<lv_obj_t*>(lv_event_get_target(e));NetworkService::instance().setWifiEnabled(lv_obj_has_state(sw,LV_STATE_CHECKED));}
void UiShell::controlEnergy(lv_event_t* e){auto* sw=static_cast<lv_obj_t*>(lv_event_get_target(e));auto s=SettingsService::instance().snapshot();s.powerManagement.energySaving=lv_obj_has_state(sw,LV_STATE_CHECKED);SettingsService::instance().save(s);PowerManager::instance().settingsChanged();}
void UiShell::controlClose(lv_event_t*){IdleService::instance().activity();UiShell::instance().settleControlCenter(false);}

static size_t homeTileCount(){
    auto& registry=AppRegistry::instance();registry.rebuild();size_t count=0;
    for(size_t i=0;i<registry.count();++i){auto* e=registry.at(i);if(!e||!e->launcherVisible)continue;if(e->state==AppRegistryState::Enabled)++count;}
    return min<size_t>(count,48);
}

void UiShell::appBtn(lv_event_t* e){
    const char* id=static_cast<const char*>(lv_event_get_user_data(e));
    if (!id || !id[0]) return;
    PowerManager::instance().requestBurst(2500);
    IdleService::instance().activity();
    auto& shell = UiShell::instance();
    if (!shell.pendingAppQueue_) return;
    char appId[APP_ID_CAPACITY]{};
    strlcpy(appId, id, sizeof(appId));
    UiPerformanceMonitor::instance().navigationQueued();
    xQueueOverwrite(shell.pendingAppQueue_, appId);
#if !defined(HOMEDISPLAY_PRODUCTION)
    Serial.printf("[APP-DIAG] QUEUED id=%s at_ms=%lu\n", appId,
                  static_cast<unsigned long>(millis()));
#endif
}

bool UiShell::requestAppSettings(const char* id){
    if(SecurityService::instance().tabletLocked())return false;
    if(!pendingAppQueue_)return false;
    char request[APP_ID_CAPACITY]{};
    if(id){
        if(!validQueuedAppId(id))return false;
        strlcpy(request,"@settings:",sizeof(request));strlcat(request,id,sizeof(request));
    }else strlcpy(request,"@apps",sizeof(request));
    UiPerformanceMonitor::instance().navigationQueued();
    return xQueueOverwrite(pendingAppQueue_,request)==pdPASS;
}

bool UiShell::requestAppOpen(const char* id,const char* arguments){
    if(SecurityService::instance().tabletLocked()||!validQueuedAppId(id)||!pendingAppQueue_||!arguments||strlen(arguments)>256)return false;
    // ID and parameters travel atomically in the existing one-slot UI queue.
    JsonDocument doc;if(deserializeJson(doc,arguments,DeserializationOption::NestingLimit(4))||!doc.is<JsonObject>())return false;
    if(!strcmp(id,"calendar")&&doc["view"]=="users")id="settings"; // Legacy deep link, one central editor.
    String canonical;serializeJson(doc,canonical);char request[APP_ID_CAPACITY]{};
    snprintf(request,sizeof(request),"%s\n%s",id,canonical.c_str());
    UiPerformanceMonitor::instance().navigationQueued();return xQueueOverwrite(pendingAppQueue_,request)==pdPASS;
}
bool UiShell::requestAppOpen(const char* id){
    if(SecurityService::instance().tabletLocked())return false;
    if(!validQueuedAppId(id)||!pendingAppQueue_)return false;
    char request[APP_ID_CAPACITY]{};strlcpy(request,id,sizeof(request));
    UiPerformanceMonitor::instance().navigationQueued();
    return xQueueOverwrite(pendingAppQueue_,request)==pdPASS;
}

bool UiShell::requestLauncherOpen(const char* id){
    if(desktopEditing_||int32_t(millis()-suppressLauncherUntil_)<0)return false;
    AppRegistryEntry entry;
    if(!AppRegistry::instance().lookup(id,entry)||!entry.launcherVisible||entry.state!=AppRegistryState::Enabled)return false;
    // Stable web IDs use the same deferred navigation queue as native/Lua apps.
    return requestAppOpen(id);
}
void UiShell::launcherBtn(lv_event_t* e){
    auto& shell=UiShell::instance();
    if(shell.desktopEditing_||int32_t(millis()-shell.suppressLauncherUntil_)<0)return;
    IdleService::instance().activity();
    UiShell::instance().requestLauncherOpen(static_cast<const char*>(lv_event_get_user_data(e)));
}
void UiShell::dockRemoveBtn(lv_event_t* e){
    if(!UiShell::instance().desktopEditing_)return;
    IdleService::instance().activity();
    HomeReorderController::instance().requestDockChange(static_cast<const char*>(lv_event_get_user_data(e)),true);
}
void UiShell::desktopEditEvent(lv_event_t* e){
    auto& shell=UiShell::instance();
    if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED){shell.setDesktopEditing(true);shell.suppressLauncherUntil_=millis()+800;}
    else if(lv_event_get_code(e)==LV_EVENT_CLICKED&&lv_event_get_target_obj(e)==shell.editDone_){shell.setDesktopEditing(false);shell.suppressLauncherUntil_=millis()+500;}
    IdleService::instance().activity();
}
void UiShell::setDesktopEditing(bool editing){
    if(editing&&(AppManager::instance().current()||SecurityService::instance().tabletLocked()))return;
    desktopEditing_=editing;
    if(editDone_)setHiddenIfChanged(editDone_,!editing);
    for(size_t i=0;i<dockRemoveCount_;++i)if(dockRemove_[i])setHiddenIfChanged(dockRemove_[i],!editing);
}

void UiShell::openAppsSettingsList(){
    auto& manager=AppManager::instance();
    if(auto* settings=static_cast<SettingsApp*>(manager.find("settings"))){
        settings->selectAppsPage();manager.open("settings",content_);
    }
}

void UiShell::homeBtn(lv_event_t*){UiShell::instance().showHome();IdleService::instance().activity();}

void UiShell::saverBtn(lv_event_t*){
    auto& shell=UiShell::instance();
    PowerManager::instance().requestBurst(1800);
    if(IdleService::instance().stage()!=IdleStage::Off || SettingsService::instance().wakeOnSingleTap()){
        shell.lastWakeTap_=0;
        IdleService::instance().activity();
        return;
    }
    const uint32_t now=millis();
    constexpr uint32_t doubleWakeWindowMs=900;
    if(shell.lastWakeTap_ && now-shell.lastWakeTap_<=doubleWakeWindowMs){
        shell.lastWakeTap_=0;
        IdleService::instance().activity();
    }else shell.lastWakeTap_=now;
}

void UiShell::touchActivity(lv_event_t* e) {
    auto* self=static_cast<UiShell*>(lv_event_get_user_data(e));
    const auto stage=IdleService::instance().stage();

    // Keep the deliberate double-tap policy only for a fully-off display.
    if(stage==IdleStage::Off)return;
    // Continuous physical contact is activity even without widget callbacks.
    // Limit hold updates; releasing starts the idle timeout from finger-up.
    if(lv_event_get_code(e)==LV_EVENT_PRESSING&&stage==IdleStage::Active&&
       uint32_t(millis()-IdleService::instance().lastActivityMillis())<100)return;

    IdleService::instance().activity();

    // The state changes to Active immediately, but the normal UI loop can be
    // up to one idle-period behind. Hide the saver layer in this LVGL callback
    // so it can never flash over a Control Center gesture.
    if(self&&self->saver_&&lv_obj_is_valid(self->saver_))
        setHiddenIfChanged(self->saver_,true);
}

void UiShell::mediaBtn(lv_event_t* e){MediaSessionService::instance().command((MediaCommand)(uintptr_t)lv_event_get_user_data(e));IdleService::instance().activity();}

void UiShell::rebuildHome(){
    homeDirty_=true;
    if (AppManager::instance().current()) return;
    const uint32_t homeStartedUs=micros();
    if (wallpaperRefreshPending_) {
        WallpaperService::instance().refresh();
        wallpaperRefreshPending_ = false;
    }
    setHiddenIfChanged(wallpaperLayer_, false);
    dock_=nullptr;
    editDone_=nullptr;dockRemoveCount_=0;
    lv_obj_clean(home_);lv_obj_set_style_pad_all(home_,0,0);
    lv_obj_set_style_bg_opa(home_,LV_OPA_TRANSP,0);
    auto* greeting=lv_label_create(home_);lv_label_set_text(greeting,"Meine Apps");lv_obj_set_style_text_font(greeting,fontWithGermanFallback(&lv_font_montserrat_28),0);
    lv_obj_remove_event_cb(home_,desktopEditEvent);
    lv_obj_remove_event_cb(home_,homeGesture);
    // LVGL resolves gestures to the first ancestor without GESTURE_BUBBLE.
    lv_obj_remove_flag(home_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(home_,homeGesture,LV_EVENT_GESTURE,nullptr);
    lv_obj_add_event_cb(home_,desktopEditEvent,LV_EVENT_LONG_PRESSED,nullptr);
    editDone_=lv_button_create(home_);lv_obj_set_size(editDone_,120,44);lv_obj_align(editDone_,LV_ALIGN_TOP_MID,0,22);
    auto* doneLabel=lv_label_create(editDone_);lv_label_set_text(doneLabel,"Fertig");lv_obj_center(doneLabel);
    lv_obj_add_event_cb(editDone_,desktopEditEvent,LV_EVENT_CLICKED,nullptr);setHiddenIfChanged(editDone_,!desktopEditing_);
    lv_obj_set_style_text_color(greeting,UiTheme::instance().textPrimary(),0);lv_obj_align(greeting,LV_ALIGN_TOP_LEFT,76,30);
#if defined(HOMEDISPLAY_DEVELOPMENT)
    auto* appDiag=lv_label_create(home_);lv_label_set_text(appDiag,"APP-DIAG-01");lv_obj_set_style_text_font(appDiag,fontWithGermanFallback(&lv_font_montserrat_14),0);
    lv_obj_set_style_text_color(appDiag,UiTheme::instance().accent(),0);lv_obj_align(appDiag,LV_ALIGN_TOP_RIGHT,-26,26);
#endif
    auto* iconGrid=lv_obj_create(home_);lv_obj_set_size(iconGrid,1080,430);lv_obj_align(iconGrid,LV_ALIGN_TOP_MID,0,86);
    lv_obj_clear_flag(iconGrid,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(iconGrid,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(iconGrid,0,0);lv_obj_set_flex_flow(iconGrid,LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(iconGrid,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);lv_obj_set_style_pad_row(iconGrid,30,0);lv_obj_set_style_pad_column(iconGrid,58,0);
    auto& registry=AppRegistry::instance();registry.rebuild();
    struct HomeTile{const AppRegistryEntry* entry=nullptr;};HomeTile tiles[48];size_t tileCount=0;
    for(size_t i=0;i<registry.count()&&tileCount<48;++i){auto* e=registry.at(i);if(!e||!e->launcherVisible||e->state!=AppRegistryState::Enabled)continue;tiles[tileCount++]={e};}
    constexpr size_t appsPerPage=10;size_t pageCount=max<size_t>(1,(tileCount+appsPerPage-1)/appsPerPage);if(homePage_>=pageCount)homePage_=(uint8_t)(pageCount-1);
    size_t firstApp=homePage_*appsPerPage;
    for(size_t i=firstApp;i<min(tileCount,firstApp+appsPerPage);++i){auto* e=tiles[i].entry;if(!e)continue;bool isWeb=e->kind==AppKind::Web;
        auto* button=lv_button_create(iconGrid);lv_obj_set_size(button,160,160);UiTheme::instance().styleCard(button);
        strlcpy(homeTileIds_[i-firstApp],e->id.c_str(),sizeof(homeTileIds_[0]));
        lv_obj_set_user_data(button,homeTileIds_[i-firstApp]);
        lv_obj_add_event_cb(button,launcherBtn,LV_EVENT_CLICKED,homeTileIds_[i-firstApp]);
        auto* icon=createAppIcon(button,isWeb?LV_SYMBOL_DIRECTORY:(e->app?e->app->icon():LV_SYMBOL_HOME),48,UiTheme::instance().accent());
        lv_obj_set_style_text_color(icon,UiTheme::instance().accent(),0);lv_obj_align(icon,LV_ALIGN_TOP_MID,0,24);
        auto* label=lv_label_create(button);lv_label_set_text(label,e->name.c_str());lv_obj_set_width(label,142);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(label,fontWithGermanFallback(&lv_font_montserrat_18),0);
        lv_obj_set_style_text_color(label,UiTheme::instance().textPrimary(),0);lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,-18);}
    if(pageCount>1){
        auto* previous=makeSymbolButton(home_,LV_SYMBOL_LEFT,48,64,homePageBtn,reinterpret_cast<void*>(static_cast<intptr_t>(-1)));lv_obj_align(previous,LV_ALIGN_LEFT_MID,12,-40);
        auto* next=makeSymbolButton(home_,LV_SYMBOL_RIGHT,48,64,homePageBtn,reinterpret_cast<void*>(static_cast<intptr_t>(1)));lv_obj_align(next,LV_ALIGN_RIGHT_MID,-12,-40);
        if(!homePage_)lv_obj_add_state(previous,LV_STATE_DISABLED);
        if(homePage_+1>=pageCount)lv_obj_add_state(next,LV_STATE_DISABLED);
        auto* dots=lv_obj_create(home_);lv_obj_remove_style_all(dots);lv_obj_set_size(dots,pageCount*24,16);lv_obj_align(dots,LV_ALIGN_BOTTOM_MID,0,-143);
        for(size_t i=0;i<pageCount;++i){auto* dot=lv_obj_create(dots);lv_obj_remove_style_all(dot);lv_obj_set_size(dot,10,10);lv_obj_set_pos(dot,i*24+7,3);
            lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
            lv_obj_set_style_bg_color(dot,i==homePage_?UiTheme::instance().accent():UiTheme::instance().textSecondary(),0);}
    }
    rebuildDock();
    ++homeRevision_;
    homeDirty_=false;
    setHiddenIfChanged(home_,false);setHiddenIfChanged(content_,true);
    UiPerformanceMonitor::instance().homeBuild(micros()-homeStartedUs);
}

void UiShell::rebuildDock(){
    if(!home_||AppManager::instance().current())return;
    if(dock_)lv_obj_delete(dock_);
    dockRemoveCount_=0;
    dock_=lv_obj_create(home_);lv_obj_set_size(dock_,1120,112);
    lv_obj_align(dock_,LV_ALIGN_BOTTOM_MID,0,-22);UiTheme::instance().styleGlassPanel(dock_);
    lv_obj_set_style_pad_all(dock_,8,0);
    lv_obj_add_event_cb(dock_,desktopEditEvent,LV_EVENT_LONG_PRESSED,nullptr);
    lv_obj_set_style_pad_column(dock_,18,0);
    lv_obj_set_scroll_dir(dock_,LV_DIR_HOR);
    lv_obj_set_flex_flow(dock_,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock_,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    const size_t count=DockShortcutService::instance().load(dockIds_,DockShortcutService::kMaxItems);
    size_t visible=0;
    for(size_t i=0;i<count;++i){
        AppRegistryEntry entry;
        if(!AppRegistry::instance().lookup(dockIds_[i],entry)||!entry.launcherVisible||entry.state!=AppRegistryState::Enabled)continue;
        ++visible;
        auto* item=lv_obj_create(dock_);lv_obj_remove_style_all(item);lv_obj_set_size(item,110,90);
        lv_obj_clear_flag(item,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(item,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        auto* button=makeSymbolButton(item,entry.app?entry.app->icon():LV_SYMBOL_DIRECTORY,58,58,launcherBtn,dockIds_[i]);
        lv_obj_align(button,LV_ALIGN_TOP_MID,0,0);
        lv_obj_add_event_cb(button,desktopEditEvent,LV_EVENT_LONG_PRESSED,nullptr);
        auto* label=lv_label_create(item);lv_label_set_text(label,entry.name.c_str());lv_obj_set_width(label,106);
        lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_font(label,fontWithGermanFallback(&lv_font_montserrat_14),0);
        lv_obj_set_style_text_color(label,UiTheme::instance().textPrimary(),0);lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,0);
        auto* remove=makeSymbolButton(item,LV_SYMBOL_CLOSE,28,28,dockRemoveBtn,dockIds_[i]);
        lv_obj_align(remove,LV_ALIGN_TOP_RIGHT,0,0);
        dockRemove_[dockRemoveCount_++]=remove;setHiddenIfChanged(remove,!desktopEditing_);
    }
    if(!visible){auto* hint=lv_label_create(dock_);lv_label_set_text(hint,"App lange druecken und hierher ziehen");lv_obj_set_style_text_color(hint,UiTheme::instance().textPrimary(),0);}
    dockDirty_=false;
}

void UiShell::requestDockRefresh() {
    dockDirty_.store(true,std::memory_order_release);
}

void UiShell::homePageBtn(lv_event_t* e){UiShell::instance().pendingHomePageChange_.store((int8_t)(intptr_t)lv_event_get_user_data(e));}
void UiShell::homeGesture(lv_event_t*){
    auto& s=UiShell::instance();if(s.desktopEditing_||AppManager::instance().current())return;
    auto* indev=lv_indev_active();if(!indev)return;
    const auto dir=lv_indev_get_gesture_dir(indev);
    if(dir==LV_DIR_LEFT||dir==LV_DIR_RIGHT){s.pendingHomePageChange_.store(dir==LV_DIR_LEFT?1:-1);s.suppressLauncherUntil_=millis()+350;}
}
void UiShell::changeHomePage(int change){
    if(!change||HomeReorderController::instance().dragging()||AppManager::instance().current())return;
    const size_t pages=max<size_t>(1,(homeTileCount()+9)/10);
    const int target=constrain((int)homePage_+change,0,(int)pages-1);
    if(target!=homePage_){homePage_=(uint8_t)target;rebuildHome();}
}

void UiShell::showHome(){
    if(SecurityService::instance().tabletLocked())return;
    setHiddenIfChanged(appSettingsBack_,true);setHiddenIfChanged(subtitle_,false);
    SecurityService::instance().lockSettings();
    AppManager::instance().suspendCurrent();
    setAppFullscreen(content_,false);
    if(homeDirty_)rebuildHome();
    else if(dockDirty_)rebuildDock();
    setHiddenIfChanged(content_,true);setHiddenIfChanged(home_,false);
    setHiddenIfChanged(wallpaperLayer_,false);
    IdleService::instance().setContext(IdleContext::Normal);
}

void UiShell::hideHomeBackground(lv_obj_t* host) {
    if (host != content_) return;
    setHiddenIfChanged(appSettingsBack_,true);setHiddenIfChanged(subtitle_,false);
    setAppFullscreen(host,false);
    setDesktopEditing(false);
    setHiddenIfChanged(home_,true);setHiddenIfChanged(content_,false);
    setHiddenIfChanged(wallpaperLayer_, true);
}

void UiShell::setAppFullscreen(lv_obj_t* host,bool enabled){
    if(!content_||host!=content_)return;
    setHiddenIfChanged(top_,enabled);if(edge_)setHiddenIfChanged(edge_,enabled);
    lv_obj_set_pos(content_,0,enabled?0:76);
    lv_obj_set_size(content_,LV_PCT(100),enabled?LV_PCT(100):724);
    lv_obj_set_style_pad_all(content_,enabled?0:26,0);
}

void UiShell::appsChanged(){requestAppsRefresh();}

void UiShell::requestAppsRefresh(){
    homeDirty_.store(true,std::memory_order_release);
    appsRefreshPending_.store(true, std::memory_order_release);
    LuaAppRegistry::instance().requestBackgroundScan();
}

void UiShell::refreshLauncherAfterInstall(){
    if (!AppManager::instance().current()) rebuildHome();
    requestAppsRefresh();
}

void UiShell::refreshTheme(){
    homeDirty_=true;
    publishLuaEvent(nullptr,"theme.changed");
    wallpaperRefreshPending_ = true;
    if (!AppManager::instance().current()) rebuildHome();
}

static void setLabelIfChanged(lv_obj_t* label, const String& value) {
    if (label && value != lv_label_get_text(label)) lv_label_set_text(label, value.c_str());
}

static void setHiddenIfChanged(lv_obj_t* obj, bool hidden) {
    if (!obj) return;
    const bool isHidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hidden == isHidden) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void UiShell::updateStatus() {
    const auto& net = NetworkService::instance();
    String netText = net.online()
        ? String(IconCatalog::network(net.usingEthernet()))
        : String(LV_SYMBOL_WARNING);
    setLabelIfChanged(statusNetwork_, netText);
    setLabelIfChanged(statusSd_, StorageService::instance().sdAvailable()
        ? String(IconCatalog::sd()) : String(""));
    setLabelIfChanged(clock_, TimeService::instance().timeText());
    setLabelIfChanged(date_, TimeService::instance().dateText());
}

void UiShell::applyIdle(){
    auto st=IdleService::instance().stage();
    auto p=IdleService::instance().profile();
    const auto settings=SettingsService::instance().snapshot();
    uint8_t target=0;

    if(st==IdleStage::Active){
        const auto ambient=AmbientLightService::instance().status();
        target=(settings.ambientLight.autoBrightness && ambient.autoBrightnessActive)
            ? ambient.targetBrightness : settings.brightness;
        setHiddenIfChanged(saver_, true);
    }else if(st==IdleStage::Dimmed){
        target=p.dimBrightness;
        setHiddenIfChanged(saver_, true);
    }else if(st==IdleStage::Saver){
        target=p.saverBrightness;
        setHiddenIfChanged(saver_, true);
        if(lv_obj_get_child_count(saver_)>2)
            setHiddenIfChanged(lv_obj_get_child(saver_,2), false);
        auto ms=MediaSessionService::instance().snapshot();
        String x=ms.available?(ms.title+"\n"+ms.subtitle):"HomePanel";
        setLabelIfChanged(media_, x);
    }else{
        if(lv_obj_get_child_count(saver_)>2)
            setHiddenIfChanged(lv_obj_get_child(saver_,2), true);
        if(lv_obj_get_child_count(saver_)>=2){
            setLabelIfChanged(lv_obj_get_child(saver_,0),TimeService::instance().timeText());
            setLabelIfChanged(lv_obj_get_child(saver_,1),TimeService::instance().dateText());
        }
        setHiddenIfChanged(saver_, false);
        target=p.alwaysOnEnabled?p.alwaysOnBrightness:0;
    }

    if (lastAppliedBrightness_ != target &&
        DisplayManager::instance().brightness() != target)
        DisplayManager::instance().setBrightness(target);
    lastAppliedBrightness_=DisplayManager::instance().brightness();
}

void UiShell::loop(){SystemSupervisor::instance().heartbeat(ServiceId::Ui);BootHealthCheck::instance().reportEventLoop();static uint32_t lastUi=0;auto idle=IdleService::instance().stage();
    uint32_t uiPeriod=DisplayManager::instance().bspManagesLvgl()?25:5;if(idle==IdleStage::Dimmed)uiPeriod=50;else if(idle==IdleStage::Saver)uiPeriod=100;else if(idle==IdleStage::Off)uiPeriod=200;
    const bool urgent=idle!=appliedIdleStage_ || CalendarReminderService::instance().urgent() || IntercomService::instance().urgent() || VideoService::instance().uiPending() ||
        pendingHomePageChange_.load()!=0 || (pendingAppQueue_&&uxQueueMessagesWaiting(pendingAppQueue_)>0);
    if(!urgent&&millis()-lastUi<uiPeriod)return;
    if(urgent)UiPerformanceMonitor::instance().urgentUpdate();
    lastUi=millis();
    const bool clockDue=millis()-lastClock_>1000;
    const bool stageChanged=idle!=appliedIdleStage_;
    const uint32_t idleRefreshMs =
        idle==IdleStage::Saver ? 2000UL : 1000UL;
    const bool idleDue=stageChanged || millis()-lastIdleApply_>=idleRefreshMs;
    static uint32_t lastAppPoll=0;
    const bool installPending = LuaPackageInstaller::instance().uiCommitPending();
    const bool appOpenPending = pendingAppQueue_ && uxQueueMessagesWaiting(pendingAppQueue_) > 0;
    const bool appsRefreshDue=appsRefreshPending_.load(std::memory_order_acquire) && millis()-lastAppPoll>=100;
    if(appsRefreshDue)lastAppPoll=millis();
    static uint32_t lastTouchProbe = 0;
    const bool touchProbeDue = (uint32_t)(millis() - lastTouchProbe) >= 100UL;
    const bool wallpaperDue=WallpaperService::instance().needsRefresh();
    const bool dockDue=dockDirty_.load(std::memory_order_acquire)&&!AppManager::instance().current();
    auto* foreground=AppManager::instance().current();
    const bool luaUiDue=foreground&&foreground->kind()==AppKind::Lua&&static_cast<LuaApp*>(foreground)->uiWorkPending();
    if(DisplayManager::instance().bspManagesLvgl() && !pendingHomePageChange_.load() && !clockDue &&
       !idleDue && !appsRefreshDue && !touchProbeDue && !installPending &&
       !appOpenPending && !wallpaperDue && !dockDue && !luaUiDue && !CalendarReminderService::instance().urgent() && !IntercomService::instance().urgent() && !VideoService::instance().uiPending() && !AppBackgroundScheduler::instance().luaPending()) return;
    const uint32_t uiLockTimeoutMs = (installPending || appOpenPending) ? 1000 : 100;
    DisplayUiGuard uiGuard(uiLockTimeoutMs);
    if(!uiGuard)return;
    const uint32_t frameStart=micros();
    changeHomePage(pendingHomePageChange_.exchange(0));
    pollLuaVideoViews();
    uint32_t phaseStart=frameStart;
    SecurityUi::sync();
    IntercomService::instance().loopUi();
    IntercomSnapshot callSnapshot;
    const bool callActive=IntercomService::instance().snapshot(callSnapshot)&&
        (callSnapshot.state==IntercomState::Ringing||callSnapshot.state==IntercomState::Connecting||callSnapshot.state==IntercomState::Conversation);
    CalendarReminderService::instance().loopUi(callActive);
    PhotoLibraryService::instance().pollUi(callActive);
    LanguageService::instance().pollUi();
    if(wallpaperDue)WallpaperService::instance().poll();
    static LuaApp* suspended=nullptr;
    const bool suspendedWasRemovable=suspended&&(suspended->assetPackageOnUsb()||!suspended->assetPackageOnAppFs());
    if(LuaAppRegistry::instance().refreshUsbPackages()){
        homeDirty_=true;
        if(suspendedWasRemovable)suspended=nullptr;
        AppRegistry::instance().rebuild();
        DynamicAppSettings::instance().invalidate();
        DynamicAppSettings::instance().update();
        appsRefreshPending_.store(true,std::memory_order_release);
        if(!AppManager::instance().current())rebuildHome();
    }
    if(SecurityService::instance().tabletLocked()) {
        controlPull_.active=false;if(controlCenter_){lv_anim_delete(controlCenter_,nullptr);setHiddenIfChanged(controlCenter_,true);}
        if(pendingAppQueue_)xQueueReset(pendingAppQueue_);
        auto* app=AppManager::instance().current();
        if(app&&app->kind()==AppKind::Lua&&!suspended){suspended=static_cast<LuaApp*>(app);suspended->pause();}
        if(idleDue){applyIdle();appliedIdleStage_=idle;lastIdleApply_=millis();}
        BootHealthCheck::instance().reportFrame();return;
    }
    if(suspended){suspended->resume();suspended=nullptr;}
    recordPhase(Phase::Resources,micros()-phaseStart);phaseStart=micros();
    if (installPending) LuaPackageInstaller::instance().processPendingUiCommit();
    char appId[APP_ID_CAPACITY]{};
    if (pendingAppQueue_ && xQueueReceive(pendingAppQueue_, appId, 0) == pdTRUE) {
        if(!strcmp(appId,"@apps"))openAppsSettingsList();
        else if(!strncmp(appId,"@settings:",10)) {
            if(AppManager::instance().openSettings(appId+10,content_)) {
                setHiddenIfChanged(subtitle_,true);setHiddenIfChanged(appSettingsBack_,false);
            }else openAppsSettingsList();
        }else if(char* arguments=strchr(appId,'\n')){*arguments++='\0';AppManager::instance().openWithArguments(appId,content_,arguments);}
        else if(!strncmp(appId,"web-",4)){
            AppRegistryEntry entry;
            if(AppRegistry::instance().lookup(appId,entry)&&entry.kind==AppKind::Web){
                if(entry.state==AppRegistryState::Enabled&&AppManager::instance().open("browser",content_))
                    static_cast<BrowserApp*>(AppManager::instance().current())->openSaved(entry.backendIndex);
            }else AppManager::instance().open(appId,content_);
        }else AppManager::instance().open(appId, content_);
    }
    recordPhase(Phase::Navigation,micros()-phaseStart);phaseStart=micros();
    AppIdleActionService::instance().poll();
    AppManager::instance().serviceCurrentUi();
    pollLuaWallpaperPreview();
    DynamicAppSettings::instance().update();
    const bool backgroundUsed=LuaApp::dispatchBackground();
    LuaApp::dispatchEvents(!backgroundUsed);
    recordPhase(Phase::Callbacks,micros()-phaseStart);phaseStart=micros();
    if(touchProbeDue) {
        lastTouchProbe=millis();
        lv_indev_t* indev=lv_indev_get_next(nullptr);
        while(indev) {
            if(lv_indev_get_type(indev)==LV_INDEV_TYPE_POINTER &&
               lv_indev_get_state(indev)==LV_INDEV_STATE_PRESSED) {
                PowerManager::instance().requestBurst(1800);
                // Keep idle alive for the complete physical press, even if no
                // LVGL widget emits another PRESSED event during a long drag.
                if(IdleService::instance().stage()!=IdleStage::Off)
                    IdleService::instance().activity();
                break;
            }
            indev=lv_indev_get_next(indev);
        }
    }
    if(appsRefreshDue && !AppManager::instance().current()) {
        appsRefreshPending_.exchange(false, std::memory_order_acq_rel);
        if(LuaAppRegistry::instance().applyBackgroundScan()) {
            rebuildHome();
        } else {
            appsRefreshPending_.store(true, std::memory_order_release);
        }
    }
    if(clockDue){
        lastClock_=millis();
        updateStatus();
        if(idle==IdleStage::Off && saver_ && lv_obj_get_child_count(saver_)>=2){
            setLabelIfChanged(lv_obj_get_child(saver_,0),TimeService::instance().timeText());
            setLabelIfChanged(lv_obj_get_child(saver_,1),TimeService::instance().dateText());
        }
    }
    // Dock-only edits never rebuild the permanent desktop or app host.
    if(dockDue&&dockDirty_&&!AppManager::instance().current())rebuildDock();
    if(idleDue){
        applyIdle();
        // Re-read the stage after input callbacks may have promoted it to Active.
        // Never remember an obsolete Saver/Off snapshot from the top of loop().
        appliedIdleStage_=IdleService::instance().stage();
        lastIdleApply_=millis();
    }
    DisplayManager::instance().serviceLvgl();
    recordPhase(Phase::Housekeeping,micros()-phaseStart);
uint32_t frameUs=micros()-frameStart;UiPerformanceMonitor::instance().uiLoop(frameUs);if(frameUs>maxFrameUs_)maxFrameUs_=frameUs;++frameCount_;++fpsWindowFrames_;if(!fpsWindowStart_)fpsWindowStart_=millis();
    if(millis()-fpsWindowStart_>=1000){fps_=(uint16_t)(fpsWindowFrames_*1000UL/max<uint32_t>(1,millis()-fpsWindowStart_));fpsWindowFrames_=0;fpsWindowStart_=millis();}BootHealthCheck::instance().reportFrame();}
}
