#include "AppManager.h"
#include "../services/UiPerformanceMonitor.h"
#include "EventBus.h"
#include "EventLog.h"
#include "../ui/UiTheme.h"
#include "../ui/UiFonts.h"
#include "../ui/UiShell.h"
#include "../services/ConfigService.h"
#include "../services/SecurityService.h"
#include "../services/PowerManager.h"
#include "../apps/lua/LuaApp.h"
#include "../apps/lua/LuaJobService.h"
#include "AppBackgroundScheduler.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <ArduinoJson.h>

namespace hp {

namespace {

void traceAppHost(const char* stage, const char* id, lv_obj_t* host) {
#if defined(HOMEDISPLAY_PRODUCTION)
    (void)stage;(void)id;(void)host;
#else
    if (!host) {
        Serial.printf("[APP-DIAG] %s id=%s host=null\n", stage, id ? id : "?");
        return;
    }
    Serial.printf(
        "[APP-DIAG] %s id=%s host=%p parent=%p screen=%p children=%u hidden=%u opa=%u pos=%d,%d size=%d,%d\n",
        stage, id ? id : "?", host, lv_obj_get_parent(host), lv_screen_active(),
        static_cast<unsigned>(lv_obj_get_child_count(host)),
        lv_obj_has_flag(host, LV_OBJ_FLAG_HIDDEN) ? 1u : 0u,
        static_cast<unsigned>(lv_obj_get_style_opa(host, LV_PART_MAIN)),
        static_cast<int>(lv_obj_get_x(host)), static_cast<int>(lv_obj_get_y(host)),
        static_cast<int>(lv_obj_get_width(host)), static_cast<int>(lv_obj_get_height(host))
    );
#endif
}

}

AppManager& AppManager::instance() {
    static AppManager m;
    return m;
}

const char* AppManager::runtimeStateName(AppRuntimeState state) {
    switch (state) {
        case AppRuntimeState::Ready: return "Bereit";
        case AppRuntimeState::Opening: return "Startet";
        case AppRuntimeState::Open: return "Geoeffnet";
        case AppRuntimeState::Settings: return "Einstellungen";
        case AppRuntimeState::Closed: return "Geschlossen";
        case AppRuntimeState::Error: return "Fehler";
        case AppRuntimeState::Suspended: return "Pausiert";
        case AppRuntimeState::Evicted: return "Freigegeben";
    }
    return "?";
}

void AppManager::setRuntime(IApp* app, AppRuntimeState state, const String& detail) {
    if (!app) return;
    for (size_t i = 0; i < n_; ++i) {
        if (apps_[i] != app) continue;
        runtime_[i].state = state;
        runtime_[i].detail = detail;
        runtime_[i].changedAtMs = millis();
        return;
    }
}

AppRuntimeStatus AppManager::runtimeStatus(const char* id) const {
    if (!id || !id[0]) return {AppRuntimeState::Error, "Unbekannte App", millis()};
    for (size_t i = 0; i < n_; ++i) {
        if (apps_[i] && std::strcmp(apps_[i]->id(), id) == 0) return runtime_[i];
    }
    return {AppRuntimeState::Error, "Nicht registriert", millis()};
}

bool AppManager::registerApp(IApp* app) {
    if (!app || !app->id() || !app->id()[0] || n_ >= MAX) return false;

    for (size_t i = 0; i < n_; ++i) {
        if (apps_[i] == app || std::strcmp(apps_[i]->id(), app->id()) == 0) {
            return false;
        }
    }

    if (!app->begin()) {
        EventLog::instance().add("ERROR", "APP_BEGIN_FAILED", app->id());
        return false;
    }
    apps_[n_] = app;
    runtime_[n_] = {AppRuntimeState::Ready, "", millis()};
    ++n_;
    return true;
}

bool AppManager::unregisterApp(const char* id) {
    if (!id || !id[0]) return false;

    for (size_t i = 0; i < n_; ++i) {
        IApp* app = apps_[i];
        if (!app || std::strcmp(app->id(), id) != 0) continue;

        if (current_ == app) {
            current_->close();
            current_ = nullptr;
        }
        suspended_.remove(app);
        if(app->kind()==AppKind::Lua)app->close();
        AppBackgroundScheduler::instance().remove(id);

        for (size_t j = i + 1; j < n_; ++j) {
            apps_[j - 1] = apps_[j];
            runtime_[j - 1] = runtime_[j];
        }
        apps_[--n_] = nullptr;
        runtime_[n_] = {};
        EventLog::instance().add("INFO", "APP_UNREGISTER", id);
        return true;
    }

    return false;
}

IApp* AppManager::find(const char* id) const {
    if (!id || !id[0]) return nullptr;
    for (size_t i = 0; i < n_; ++i) {
        if (apps_[i] && std::strcmp(apps_[i]->id(), id) == 0) return apps_[i];
    }
    return nullptr;
}

bool AppManager::isEnabled(const IApp* app) const {
    if (!app) return false;
    if (!app->userInstallable()) return true;

    auto& config = ConfigService::instance();
    return config.appEnabled(app->id(),app->enabledByDefault());
}

bool AppManager::isEnabled(const char* id) const {
    return isEnabled(find(id));
}

bool AppManager::setEnabled(const IApp* app, bool enabled, String* error) {
    if(error)*error="";
    if (!app || !app->userInstallable()){if(error)*error="app_not_installable";return false;}
    const auto result=ConfigService::instance().saveAppEnabled(app->id(),enabled);
    const bool saved=result==ConfigService::AppStateWriteResult::Ok;
    if(error&&!saved){switch(result){
        case ConfigService::AppStateWriteResult::NotWritable:*error="nvs_not_writable";break;
        case ConfigService::AppStateWriteResult::InvalidIdentity:*error="nvs_invalid_app_identity";break;
        case ConfigService::AppStateWriteResult::InvalidRecord:*error="nvs_app_state_record_invalid";break;
        default:*error="nvs_write_failed";break;
    }}
    if(saved&&!enabled){disabledCleanup_=true;AppBackgroundScheduler::instance().remove(app->id());if(app->kind()==AppKind::Lua)LuaJobService::instance().cancelOwner(app->id());}return saved;
}

bool AppManager::setEnabled(const char* id, bool enabled) {
    return setEnabled(find(id), enabled);
}

bool AppManager::openWithArguments(const char* id,lv_obj_t* parent,const String& arguments){
    if(arguments.length()>256)return false;JsonDocument doc;
    if(deserializeJson(doc,arguments,DeserializationOption::NestingLimit(4))||!doc.is<JsonObject>())return false;
    launchArguments_=arguments;const bool ok=open(id,parent);launchArguments_="{}";
    if(ok&&current_)current_->launchArgumentsChanged();return ok;
}
bool AppManager::open(const char* id, lv_obj_t* parent) {
    if(SecurityService::instance().tabletLocked())return false;
    if (!id || !id[0] || !parent) return false;
    const uint32_t openStartedMs = millis();
    const uint32_t openStartedUs = micros();

    // These trace markers are already sent to USB. Retain them in the RAM
    // ring, but never synchronously flush LittleFS while holding the UI lock.
    EventLog::instance().addTransient("INFO", "APP_OPEN_REQ", id);
    traceAppHost("OPEN_REQUEST", id, parent);

    IApp* app = find(id);
    if (!app) return false;

    // Disabled user apps must not be launchable through stale UI/event handles.
    if (app->userInstallable() && !isEnabled(app)) {
        EventLog::instance().add("WARN", "APP_DISABLED", id);
        return false;
    }

    EventLog::instance().addTransient("INFO", "APP_REGISTRY_FOUND", id);
    traceAppHost("REGISTRY_FOUND", id, parent);

    app->setLaunchArguments(launchArguments_);
    if (current_ == app && runtimeStatus(id).state == AppRuntimeState::Open) return true;
    if (current_ == app && runtimeStatus(id).state == AppRuntimeState::Opening &&
        app->kind()==AppKind::Lua && !static_cast<LuaApp*>(app)->settingsState()) return true;
    SecurityService::instance().lockSettings();
    PowerManager::instance().requestBurst(1800); // Never exceeds the configured 360-MHz legacy profile.
    UiPerformanceMonitor::instance().navigationQueued(true);

    suspended_.remove(app); // Never evict the requested resume target while parking current.
    suspendCurrent();
    // The native month view allocates LVGL draw tasks immediately. If the
    // internal heap is fragmented, free only parked Lua views before starting
    // the calendar. The packages and their persistent data are untouched.
    if (!strcmp(id, "calendar")) {
        trimSuspended();
        while (suspended_.count &&
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 96 * 1024)
            evict(suspended_.oldest());
    }
    current_ = app;
    AppBackgroundScheduler::instance().setForeground(id);
    setRuntime(current_, AppRuntimeState::Opening);
    // App roots may intentionally be transparent for glass styling. Keep the
    // shared host opaque so stale Home/wallpaper pixels cannot show through
    // while its old children are removed and the new view is built.
    UiShell::instance().hideHomeBackground(parent);
    UiTheme::instance().styleScreen(parent);
    // Own the view lifecycle centrally, including apps that do not clean the
    // host themselves. The previous app has already stopped its UI timers.
    // Cold Lua starts already clear the parent in LuaApp::prepareState.
    // Parked Lua views do NOT, so clear once before reparenting that view.
    // Native apps continue to receive the pre-cleared host as before.
    const bool luaApp=app->kind()==AppKind::Lua;
    const bool parkedLua=luaApp&&static_cast<LuaApp*>(app)->lifecycle()==LuaRuntimeState::Suspended;
    if(!luaApp||parkedLua)lv_obj_clean(parent);
    const bool resumed=luaApp&&static_cast<LuaApp*>(app)->resumeTo(parent);
    if(!resumed)current_->open(parent);

    lv_obj_update_layout(parent);
    // One invalidation per transition erases the complete old view, even if
    // the new app only creates a small/transparent child tree.
    lv_obj_invalidate(parent);
    const uint32_t childCount = lv_obj_get_child_count(parent);
    traceAppHost("VIEW_LAYOUT", id, parent);
    if (childCount == 0) {
        EventLog::instance().add("ERROR", "APP_VIEW_EMPTY", id);
        UiPerformanceMonitor::instance().navigationFailed();
        setRuntime(current_, AppRuntimeState::Error, "App erzeugte keine sichtbare Ansicht");
        current_->close();
        current_ = nullptr;
        AppBackgroundScheduler::instance().setForeground(nullptr);

        lv_obj_t* failure = lv_obj_create(parent);
        lv_obj_set_size(failure, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(failure, lv_color_hex(0x18243A), 0);
        lv_obj_set_style_bg_opa(failure, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(failure, 0, 0);
        lv_obj_t* message = lv_label_create(failure);
        lv_label_set_text_fmt(message, "App konnte nicht geoeffnet werden:\n%s", id);
        lv_obj_set_style_text_color(message, lv_color_hex(0xF4F7FF), 0);
        lv_obj_set_style_text_font(message, fontWithGermanFallback(&lv_font_montserrat_24), 0);
        lv_obj_center(message);
        return false;
    }

    const bool preparing=luaApp&&static_cast<LuaApp*>(app)->opening();
    setRuntime(current_, preparing?AppRuntimeState::Opening:AppRuntimeState::Open);
    if(!preparing){UiPerformanceMonitor::instance().appBuild(micros()-openStartedUs,resumed);UiPerformanceMonitor::instance().navigationBuilt();}
    trimSuspended();
    if(resumed)EventLog::instance().addTransient("INFO","APP_RESUME",id);
#if !defined(HOMEDISPLAY_PRODUCTION)
    Serial.printf("[APP-DIAG] BUILD_READY id=%s elapsed_ms=%lu at_ms=%lu\n", id,
                  static_cast<unsigned long>(millis() - openStartedMs),
                  static_cast<unsigned long>(millis()));
#endif
    EventLog::instance().addTransient("INFO", "APP_VIEW_READY", String(id) + " children=" + String(childCount));
    EventBus::instance().publish({EventType::AppOpened, current_});
    return true;
}


bool AppManager::openSettings(const char* id, lv_obj_t* parent) {
    if(SecurityService::instance().tabletLocked())return false;
    const uint32_t settingsStartedUs=micros();
    SecurityService::instance().lockSettings();
    if (!id || !id[0] || !parent) return false;

    IApp* app = find(id);
    if (!app || !app->hasSettings()) return false;

    if (app->userInstallable() && !isEnabled(app)) return false;

    PowerManager::instance().requestBurst(1800);
    UiPerformanceMonitor::instance().navigationQueued(true);
    suspended_.remove(app);
    suspendCurrent();
    if (!strcmp(id, "calendar")) trimSuspended();
    if(app->kind()==AppKind::Lua)app->close(); // Never retain a separate settings state.
    current_ = app;
    AppBackgroundScheduler::instance().setForeground(id);

    // Settings views share the launcher host, so give them a deterministic
    // opaque backdrop before clearing/building their child tree.
    UiShell::instance().hideHomeBackground(parent);
    UiTheme::instance().styleScreen(parent);
    if(app->kind()!=AppKind::Lua)lv_obj_clean(parent);
    setRuntime(current_, AppRuntimeState::Opening, "Einstellungen");
    if (!current_->openSettings(parent)) {
        UiPerformanceMonitor::instance().navigationFailed();
        setRuntime(current_, AppRuntimeState::Error, "Einstellungen konnten nicht geoeffnet werden");
        current_->close();
        current_ = nullptr;
        AppBackgroundScheduler::instance().setForeground(nullptr);
        return false;
    }

    lv_obj_update_layout(parent);
    lv_obj_invalidate(parent);
    const bool preparing=app->kind()==AppKind::Lua&&static_cast<LuaApp*>(app)->opening();
    setRuntime(current_, preparing?AppRuntimeState::Opening:AppRuntimeState::Settings);
    if(!preparing){UiPerformanceMonitor::instance().appBuild(micros()-settingsStartedUs);UiPerformanceMonitor::instance().navigationBuilt();}
    EventLog::instance().addTransient("INFO", "APP_SETTINGS_OPEN", id);
    return true;
}

void AppManager::closeCurrent() {
    SecurityService::instance().lockSettings();
    if (!current_) return;
    IApp* closing = current_;
    closing->close();
    setRuntime(closing, AppRuntimeState::Closed);
    current_ = nullptr;
    AppBackgroundScheduler::instance().setForeground(nullptr);
}
void AppManager::serviceCurrentUi(){
    // setEnabled can be called by the web task or an LVGL callback. Destruction
    // is deferred to this guarded orchestration pass, never done in either.
    if(disabledCleanup_.exchange(false)){
        for(size_t i=0;i<n_;++i)if(apps_[i]->kind()==AppKind::Lua&&!isEnabled(apps_[i])&&apps_[i]!=current_&&
            static_cast<LuaApp*>(apps_[i])->lifecycle()==LuaRuntimeState::Suspended)
            evict(apps_[i]);
    }
    if(current_&&current_->kind()!=AppKind::Lua&&current_->userInstallable()&&!isEnabled(current_)){
        closeCurrent();UiShell::instance().showHome();return;
    }
    if(!current_||current_->kind()!=AppKind::Lua)return;
    auto* app=static_cast<LuaApp*>(current_);
    if(!isEnabled(app)&&!app->settingsState()){closeCurrent();UiShell::instance().showHome();return;}
    app->serviceUi();
    if(app->failed()){setRuntime(app,AppRuntimeState::Error,"Lua-App gestoppt");UiPerformanceMonitor::instance().navigationFailed();}
    else if(!app->opening()&&runtimeStatus(app->id()).state==AppRuntimeState::Opening)
        setRuntime(app,app->settingsState()?AppRuntimeState::Settings:AppRuntimeState::Open);
}

void AppManager::evict(IApp* app){if(!app||app==current_||app->kind()!=AppKind::Lua)return;suspended_.remove(app);static_cast<LuaApp*>(app)->evict();setRuntime(app,AppRuntimeState::Evicted);++evictions_;EventLog::instance().addTransient("INFO","APP_EVICT",app->id());}
void AppManager::trimSuspended(){while(suspended_.count&&(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<64*1024||heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<2*1024*1024))evict(suspended_.oldest());}
void AppManager::suspendCurrent(){
    SecurityService::instance().lockSettings();if(!current_)return;IApp* previous=current_;
    if(previous->kind()==AppKind::Lua&&!static_cast<LuaApp*>(previous)->settingsState()){
        if(!parking_){parking_=lv_obj_create(lv_layer_bottom());lv_obj_add_flag(parking_,LV_OBJ_FLAG_HIDDEN);}
        if(static_cast<LuaApp*>(previous)->suspendTo(parking_)){
            current_=nullptr;setRuntime(previous,AppRuntimeState::Suspended);
            if(auto* victim=suspended_.add(previous))evict(victim);
            EventLog::instance().addTransient("INFO","APP_SUSPEND",previous->id());trimSuspended();
            AppBackgroundScheduler::instance().setForeground(nullptr);return;
        }
    }
    closeCurrent();
}
AppManager::LuaDiagnostics AppManager::luaDiagnostics()const{LuaDiagnostics out;out.evictions=evictions_;for(size_t i=0;i<n_;++i)if(apps_[i]->kind()==AppKind::Lua){auto* lua=static_cast<LuaApp*>(apps_[i]);if(lua->lifecycle()==LuaRuntimeState::Suspended||lua->lifecycle()==LuaRuntimeState::BackgroundExecuting)++out.suspended;else if(lua->lifecycle()==LuaRuntimeState::Evicted)++out.evicted;else ++out.foreground;}return out;}

}
