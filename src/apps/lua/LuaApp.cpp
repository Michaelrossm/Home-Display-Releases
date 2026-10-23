#include "LuaApp.h"
#include "../../ui/UiAmbientAnimationService.h"
#include "LuaScriptReader.h"
#include "LuaDeclarativeUi.h"
#include "LuaDataSubscriptions.h"
#include "LuaScriptPreparation.h"
#include "../../services/UiPerformanceMonitor.h"
#include "CalendarLuaBridge.h"
#include "../../services/UsbAppFileSystem.h"
#include "LuaPlatformBridge.h"
#include "LuaStreamBridge.h"
#include "LuaBackgroundRun.h"
#include "../../core/AppBackgroundScheduler.h"
#include "LuaSecurityBridge.h"
#include "../../services/SecurityService.h"
#include "LuaUiBridge.h"
#include "LuaNavigationBridge.h"
#include "LuaMediaBridge.h"
#include "LuaIntercomBridge.h"
#include "../../services/VideoService.h"
#include "LuaWidgetsBridge.h"
#include "LuaChartBridge.h"
#include "LuaWeatherEffectBridge.h"
#include "LuaJsonBridge.h"
#include "LuaTimeBridge.h"
#include "LuaNetworkBridge.h"
#include "LuaJobService.h"
#include "LuaStorageBridge.h"
#include "LuaMediaStorageBridge.h"
#include "LuaDiscoveryBridge.h"
#include "LuaEnergyBridge.h"
#include "PrinterLuaBridge.h"
#include "LuaEvents.h"
#include "../../services/NetworkService.h"
#include "../../services/TimeService.h"
#include "RadioLuaBridge.h"
#include "../../ui/UiShell.h"
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
#include "../../services/StorageService.h"
#include "../../services/ConfigService.h"
#include "../../services/IdleService.h"
#include "../../ui/UiTheme.h"
#include "../../core/EventLog.h"

namespace hp {
namespace {

constexpr const char* kRegistryKey = "hp_app";
constexpr size_t kMaxLuaScriptBytes = 196608;
constexpr int kHookStride = 2000;
constexpr uint32_t kDefaultCallbackMs = 10;
constexpr uint32_t kOpenCallbackMs = 20;
constexpr uint32_t kTickHardBudgetMs = 12;
constexpr int kDefaultHookCalls = 250;   // ~500k VM instructions
constexpr int kOpenHookCalls = 1000;     // ~2M VM instructions for app startup
constexpr int kTickHookCalls = 60;       // ~120k VM instructions per tick
int kCallStartKey = 0;
int kBudgetRegistryKey = 0;
int kWallBudgetKey = 0;

// Lua's allocator must obey realloc semantics even when an allocation starts
// in internal RAM and later grows into PSRAM. heap_caps_realloc() with a
// changed capability set corrupted that transition on the P4. Allocate the
// replacement explicitly, retain the old block on failure, then copy/free.
void* luaAppAlloc(void* context, void* ptr, size_t oldSize, size_t newSize) {
    auto& budget = *static_cast<LuaApp::MemoryBudget*>(context);
    const size_t previous = ptr ? oldSize : 0; // Without ptr, Lua passes a type tag.
    if (newSize == 0) {
        if (ptr) heap_caps_free(ptr);
        budget.used -= previous;
        return nullptr;
    }
    if (newSize > LuaApp::kMemoryLimit ||
        budget.used - previous > LuaApp::kMemoryLimit - newSize) return nullptr;
    void* replacement = heap_caps_malloc(
        newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!replacement)
        replacement = heap_caps_malloc(newSize, MALLOC_CAP_8BIT);
    if (!replacement) return nullptr;
    if (ptr) {
        memcpy(replacement, ptr, oldSize < newSize ? oldSize : newSize);
        heap_caps_free(ptr);
    }
    budget.used = budget.used - previous + newSize;
    if (budget.used > budget.peak) budget.peak = budget.used;
    return replacement;
}

void instructionBudget(lua_State* L, lua_Debug*) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kBudgetRegistryKey);
    int remaining = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (--remaining <= 0) luaL_error(L, "Lua instruction budget exceeded");
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kCallStartKey);
    const uint32_t started = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kWallBudgetKey);
    const uint32_t maxMs = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (maxMs && static_cast<uint32_t>(millis() - started) > maxMs)
        luaL_error(L, "Lua callback time budget exceeded");
    lua_pushinteger(L, remaining);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kBudgetRegistryKey);
}

int protectedCall(lua_State* L, int nargs, int nresults,
                  uint32_t maxMs = kDefaultCallbackMs,
                  int maxHookCalls = kDefaultHookCalls,
                  uint32_t* elapsedUs = nullptr) {
    const uint32_t startedMs = millis();
    const uint32_t startedUs = micros();

    lua_pushinteger(L, static_cast<lua_Integer>(startedMs));
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kCallStartKey);
    lua_pushinteger(L, maxHookCalls);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kBudgetRegistryKey);
    // Use the same bounded foreground grace in the VM hook as after pcall.
    // A C bridge/PSRAM access or task preemption can consume the soft deadline;
    // the next Lua instruction must not quarantine an otherwise healthy app
    // while an identical callback ending inside that bridge succeeds below.
    // Instruction limits and strict background wall deadlines remain unchanged.
    auto* owner = LuaApp::self(L);
    const bool foreground = owner &&
        (owner->lifecycle() == LuaRuntimeState::Opening ||
         owner->lifecycle() == LuaRuntimeState::Foreground);
    const uint32_t hookWallMs = foreground && maxMs && maxMs < 100 ? 100 : maxMs;
    lua_pushinteger(L, static_cast<lua_Integer>(hookWallMs));
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kWallBudgetKey);

    lua_sethook(L, instructionBudget, LUA_MASKCOUNT, kHookStride);
    const int rc = lua_pcall(L, nargs, nresults, 0);
    lua_sethook(L, nullptr, 0, 0);

    const uint32_t usedUs = static_cast<uint32_t>(micros() - startedUs);
    if (elapsedUs) *elapsedUs = usedUs;

    // Native C bridges cannot be pre-empted by Lua's instruction hook.
    if (rc == LUA_OK && maxMs &&
        static_cast<uint32_t>(millis() - startedMs) > maxMs) {
        // A completed C bridge cannot be interrupted by the VM hook. LVGL,
        // PSRAM/flash contention and task preemption can exceed a 5/12-ms
        // deadline without any runaway Lua. Do not replay or destroy an app
        // after such a successful foreground call. Tick slowdown still uses
        // usedUs; real VM hook errors remain fatal. Keep a 100-ms native
        // ceiling and the original strict deadline for background execution.
        auto* app=LuaApp::self(L);
        const bool foreground=app &&
            (app->lifecycle()==LuaRuntimeState::Opening ||
             app->lifecycle()==LuaRuntimeState::Foreground);
        if(foreground && usedUs<=100000) {
            static uint32_t lastSlowReportMs=0;
            const uint32_t now=millis();
            if(!lastSlowReportMs || uint32_t(now-lastSlowReportMs)>=5000) {
                lastSlowReportMs=now;
                char diagnostic[96];
                snprintf(diagnostic,sizeof(diagnostic),"%s: %lu us (completed)",
                    app->id(),static_cast<unsigned long>(usedUs));
                EventLog::instance().addTransient("WARN","LUA_NATIVE_SLOW",diagnostic);
            }
            return LUA_OK;
        }
        if (nresults > 0) lua_pop(L, nresults);
        lua_pushliteral(L, "Lua native call exceeded callback time budget");
        return LUA_ERRRUN;
    }
    return rc;
}

void openAppLibraries(lua_State* L) {
    // Deliberately omit io, os, debug, package and native module loading.
    // Lua apps access device capabilities only through hp_* bridge functions.
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
}

void registerBridge(lua_State* L) {
    registerLuaPlatformBridge(L);
    registerLuaStreamBridge(L);
    registerLuaSecurityBridge(L);
    registerLuaUiBridge(L);
    registerBasicLuaUiBridge(L);
    registerLuaWidgetsBridge(L);
    registerLuaChartBridge(L);
    registerLuaWeatherEffectBridge(L);
    registerLuaJsonBridge(L);
    registerLuaTimeBridge(L);
    registerLuaNetworkBridge(L);
    registerLuaStorageBridge(L);
    registerLuaMediaStorageBridge(L);
    registerLuaDiscoveryBridge(L);
    registerLuaEnergyBridge(L);
    registerPrinterLuaBridge(L);
    registerCalendarLuaBridge(L);
    registerLuaNavigationBridge(L);
    registerLuaMediaBridge(L);
    registerLuaIntercomBridge(L);
    auto* app = LuaApp::self(L);
    if (app && app->allows(AppPermission::Audio)) registerRadioLuaBridge(L);
    protectLuaUiBridge(L);
    protectLuaBackgroundCalls(L);
    installLuaMediaStorageTables(L);
}

} // namespace

LuaApp* LuaApp::self(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRegistryKey);
    auto* app = static_cast<LuaApp*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return app;
}
TaskHandle_t LuaApp::backgroundUiTask_=nullptr;

bool LuaApp::dispatchEvents(bool allowBackground) {
    static uint32_t nextSample=0;
    static bool connected=false;
    static time_t minute=0,day=0;
    if(static_cast<int32_t>(millis()-nextSample)>=0) {
        nextSample=millis()+1000;
        LuaJobService::instance().cleanup();
        const bool online=NetworkService::instance().online();
        if(online!=connected){connected=online;publishLuaEvent(nullptr,online?"network.connected":"network.disconnected");publishLuaEvent(nullptr,"network.changed");}
        auto& time=TimeService::instance();
        if(time.valid()) {
            const time_t epoch=time.nowUtc();
            if(epoch/60!=minute){minute=epoch/60;publishLuaEvent(nullptr,"time.minute");}
            tm local{};
            if(time.localNow(local)) {
                const time_t date=(local.tm_year+1900)*512+local.tm_yday;
                if(date!=day){day=date;publishLuaEvent(nullptr,"time.day_changed");}
            }
        }
    }
    IApp* current=AppManager::instance().current();
    LuaApp* app=current&&current->kind()==AppKind::Lua?static_cast<LuaApp*>(current):nullptr;
    LuaEvent event{};
    // Limit callback work per UI pass; excess stays queued.
    const uint32_t eventPassStarted=micros();
    for(unsigned i=0;i<4&&uint32_t(micros()-eventPassStarted)<4000&&receiveLuaEvent(event);++i) {
        if(event.job&&event.owner[0]){const int state=LuaJobService::instance().completionState(event.owner,event.job);
            if(state<0){deferLuaEvent(event);break;}if(!state||uint32_t(millis()-event.created)>=60000)continue;}
        if(event.retryAfter&&static_cast<int32_t>(millis()-event.retryAfter)<0){deferLuaEvent(event);break;}
        if(event.owner[0]&&event.job){
            auto* owner=AppManager::instance().find(event.owner);
            if(owner&&owner->kind()==AppKind::Lua){auto* parked=static_cast<LuaApp*>(owner);
                if(parked->lifecycle_==LuaRuntimeState::Suspended&&AppManager::instance().isEnabled(owner)){
                    if(!allowBackground){deferLuaEvent(event);break;}
                    if(!AppBackgroundScheduler::instance().hasPolicy(event.owner))continue;
                    if(!AppBackgroundScheduler::instance().ownerAllowed(event.owner)){deferLuaEvent(event);break;}
                    parked->runBackground(event.name,event.job);return true;
                }
            }
        }
        if(!app||!app->L_||app->paused_||(event.owner[0]&&strcmp(event.owner,app->id())))continue;
        if(app->faulted_)continue;
        lua_State* dataState=app->L_;
        lua_getfield(dataState,LUA_REGISTRYINDEX,"hp.v3.data");lua_getfield(dataState,-1,"event");lua_remove(dataState,-2);
        lua_pushstring(dataState,event.name);if(event.job)lua_pushinteger(dataState,event.job);else lua_pushnil(dataState);
        if(protectedCall(dataState,2,0,5,50)!=LUA_OK){lua_pop(dataState,1);app->quarantine("data.event");break;}
        if(!app->eventFilter_.accepts(event.name,event.job&&event.owner[0]))continue;
        lua_State* L=app->L_;lua_getglobal(L,"on_event");
        if(!lua_isfunction(L,-1)){lua_pop(L,1);continue;}
        lua_pushstring(L,event.name);lua_newtable(L);
        if(event.job){lua_pushinteger(L,event.job);lua_setfield(L,-2,"job");}
        if(protectedCall(L,2,0,kTickHardBudgetMs,kTickHookCalls)!=LUA_OK) {
            lua_pop(L,1);EventLog::instance().addTransient("WARN","LUA_EVENT_ERROR",app->id());
            app->quarantine("on_event");
            break;
        }
    }
    return false;
}
bool LuaApp::runBackground(const char* event,uint32_t job,uint32_t* elapsedUs){
    if(!backgroundUiTask_||xTaskGetCurrentTaskHandle()!=backgroundUiTask_||faulted_||settingsState_||!L_||!paused_||lifecycle_!=LuaRuntimeState::Suspended)return false;
    lifecycle_=LuaRuntimeState::BackgroundExecuting;
    uint32_t elapsed=0;const bool ok=runLuaBackground(L_,micros,event,job,&elapsed);
    lifecycle_=LuaRuntimeState::Suspended; // No resume, reparent or timer activation.
    if(elapsedUs)*elapsedUs=elapsed;if(event)AppBackgroundScheduler::instance().recordLuaEvent(elapsed);
    if(!ok)EventLog::instance().addTransient("WARN","LUA_BACKGROUND_ERROR",id());
    return ok;
}
bool LuaApp::dispatchBackground(){char owner[32]{};auto& scheduler=AppBackgroundScheduler::instance();
    if(!scheduler.takeLuaOwner(owner))return false;uint32_t elapsed=0;
    auto& manager=AppManager::instance();auto* app=manager.find(owner);
    if(app&&app->kind()==AppKind::Lua&&manager.isEnabled(app))static_cast<LuaApp*>(app)->runBackground(nullptr,0,&elapsed);
    scheduler.finishLua(elapsed);return true;
}
void LuaApp::pause() {
    if(paused_)return;paused_=true;
    if(timer_)lv_timer_pause(timer_);
    if(hasGlobal("on_pause")&&!callGlobal("on_pause"))quarantine("on_pause");
}
void LuaApp::resume() {
    if(faulted_)return;
    if(!paused_)return;paused_=false;
    if(hasGlobal("on_resume")&&!callGlobal("on_resume")){quarantine("on_resume");return;}
    if(timer_)lv_timer_resume(timer_);
}
namespace {
void stopUiAnimations(lv_obj_t* root){if(!root)return;auto* object=root;for(;;){lv_anim_delete(object,nullptr);if(lv_obj_get_child_count(object)){object=lv_obj_get_child(object,0);continue;}
    while(object!=root){auto* parent=lv_obj_get_parent(object);const uint32_t next=lv_obj_get_index(object)+1;if(next<lv_obj_get_child_count(parent)){object=lv_obj_get_child(parent,next);break;}object=parent;}if(object==root)break;}}
}
void LuaApp::quarantine(const char* callback) {
    if(faulted_)return;
    faulted_=true;
    lifecycle_=LuaRuntimeState::Error;
    paused_=true;
    if(timer_)lv_timer_pause(timer_);
    stopUiAnimations(root_);
    LuaJobService::instance().cancelOwner(id());
    AppBackgroundScheduler::instance().remove(id());
    // Keep the existing view intact. Never close/reopen a screen or Lua state
    // from inside its callback; Home/navigation remains owned by UiShell.
    EventLog::instance().addTransient("WARN","LUA_QUARANTINED",id_);
    Serial.printf("[LUA] quarantined app=%s callback=%s\n",id(),callback);
}
bool LuaApp::suspendTo(lv_obj_t* parking){
    if(faulted_||!L_||!root_||settingsState_||!parking)return false;
    if(hasGlobal("on_suspend")&&!callGlobal("on_suspend",10,50)){quarantine("on_suspend");return false;}
    pause();if(faulted_)return false;VideoService::instance().close(id());LuaJobService::instance().cancelOwner(id(),true);
    lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN);stopUiAnimations(root_);lv_obj_set_parent(root_,parking);
    lifecycle_=LuaRuntimeState::Suspended;return true;
}
bool LuaApp::resumeTo(lv_obj_t* parent){
    if(faulted_||lifecycle_!=LuaRuntimeState::Suspended||!L_||!root_||!lv_obj_is_valid(root_)||!parent)return false;
    lv_obj_set_parent(root_,parent);lv_obj_remove_flag(root_,LV_OBJ_FLAG_HIDDEN);lifecycle_=LuaRuntimeState::Foreground;
    lv_obj_update_layout(parent); // Recompute percent sizes before the resume callback reads geometry.
    // Same Lua state and bindings, only reactivate its existing timer.
    paused_=false;
    lua_getfield(L_,LUA_REGISTRYINDEX,"hp.v3.data");lua_getfield(L_,-1,"resume");lua_remove(L_,-2);
    if(protectedCall(L_,0,0,5,50)!=LUA_OK){lua_pop(L_,1);quarantine("data.resume");return false;}
    if(hasGlobal("on_resume")&&!callGlobal("on_resume",10,50)){quarantine("on_resume");return false;}
    if(timer_)lv_timer_resume(timer_);return true;
}
void LuaApp::evict(){if(!faulted_&&L_)callGlobal("on_evict",10,50);close();}

bool LuaApp::hasSettings() const {
    if (hasSettingsCache_ >= 0) return hasSettingsCache_ == 1;
    if (separateSettings_) {
        hasSettingsCache_ = 1;
        return true;
    }
    const String script = defaultScript_ ? String(defaultScript_) : String();
    if (!script.length()) {
        hasSettingsCache_ = 0;
        return false;
    }

    // Deliberately conservative: only advertise a settings entry when the
    // script actually defines one of the supported settings callbacks.
    const bool hasSettings = script.indexOf("function on_settings") >= 0 ||
           script.indexOf("function open_settings") >= 0 ||
           script.indexOf("function settings") >= 0 ||
           script.indexOf("on_settings = function") >= 0 ||
           script.indexOf("open_settings = function") >= 0 ||
           script.indexOf("settings = function") >= 0;
    hasSettingsCache_ = hasSettings ? 1 : 0;
    return hasSettings;
}


bool LuaApp::hasGlobal(const char* fn) const {
    if (!L_ || !fn) return false;
    lua_getglobal(L_, fn);
    const bool ok = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    return ok;
}

bool LuaApp::callGlobal(const char* fn, uint32_t maxMs,
                        int maxHookCalls, uint32_t* elapsedUs) {
    if (!L_ || !fn) return false;
    lua_getglobal(L_, fn);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return false;
    }
    if (protectedCall(L_, 0, 0, maxMs, maxHookCalls, elapsedUs) != LUA_OK) {
        // Do not leak arbitrary Lua error strings (URLs/secrets). Report the
        // runtime failure class so diagnosis distinguishes limits from bugs.
        const char* message=lua_tostring(L_,-1);
        const char* reason="callback_failed";
        if(message&&strstr(message,"instruction budget exceeded"))reason="instruction_budget";
        else if(message&&strstr(message,"time budget"))reason="time_budget";
        else if(message&&strstr(message,"invalid UI object"))reason="invalid_ui_object";
        else if(message&&strstr(message,"foreign UI object"))reason="foreign_ui_object";
        else if(message&&strstr(message,"UI object budget"))reason="ui_object_budget";
        // Source line only, never arbitrary exception text (may contain secrets).
        unsigned line=0;
        if(message)for(const char* p=message;*p;++p)if(*p==':'&&p[1]>='0'&&p[1]<='9'){
            unsigned value=0;const char* q=p+1;
            while(*q>='0'&&*q<='9'&&value<100000)value=value*10+unsigned(*q++-'0');
            if(*q==':'&&value<100000){line=value;break;}
        }
        EventLog::instance().addTransient("WARN", "LUA_ERROR",id_ + ": " + fn + " " + reason+(line?String(" line=")+String(line):String()));
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

void LuaApp::eventTrampoline(lv_event_t* e) {
    if(SecurityService::instance().tabletLocked())return;
    auto* binding=static_cast<EventBinding*>(lv_event_get_user_data(e));
    if(!binding||!binding->app)return;
    if(binding->app->paused_||(binding->app->lifecycle_!=LuaRuntimeState::Foreground&&binding->app->lifecycle_!=LuaRuntimeState::Opening))return;
    auto* app=binding->app;
    const String name(binding->name); // Callback can remove its own widget.
    if(!app->callGlobal(name.c_str(),kDefaultCallbackMs,kDefaultHookCalls))
        app->quarantine(name.c_str());
    IdleService::instance().activity();
}

void LuaApp::bindEvent(lv_obj_t* object,lv_event_code_t code,const char* name) {
    if(bindings_.size()>=256||!name||strlen(name)>96)return;
    char* copy=strdup(name);
    if(!copy)return;
    auto* binding=new(std::nothrow) EventBinding{this,copy};
    if(!binding){free(copy);return;}
    bindings_.push_back(binding);
    lv_obj_add_event_cb(object,eventTrampoline,code,binding);
    lv_obj_add_event_cb(object,[](lv_event_t* event){
        auto* b=static_cast<EventBinding*>(lv_event_get_user_data(event));
        auto& bindings=b->app->bindings_;
        const auto it=std::find(bindings.begin(),bindings.end(),b);
        if(it!=bindings.end())bindings.erase(it);
        free(b->name);delete b;
    },LV_EVENT_DELETE,binding);
}
bool LuaApp::callUiEvent(const char* name,const char* value,int32_t index,int32_t count){
    if(!L_||paused_||faulted_||(lifecycle_!=LuaRuntimeState::Foreground&&lifecycle_!=LuaRuntimeState::Opening)||SecurityService::instance().tabletLocked()||!name||!name[0])return false;
    lua_getglobal(L_,name);if(!lua_isfunction(L_,-1)){lua_pop(L_,1);return false;}
    lua_pushstring(L_,value?value:"");lua_pushinteger(L_,index);lua_pushinteger(L_,count);
    const bool ok=protectedCall(L_,3,0,kDefaultCallbackMs,kDefaultHookCalls)==LUA_OK;
    if(!ok){lua_pop(L_,1);quarantine("widget callback");}return ok;
}

void LuaApp::tickCb(lv_timer_t* t) {
    if(SecurityService::instance().tabletLocked())return;
    auto* app=static_cast<LuaApp*>(lv_timer_get_user_data(t));
    if(!app||!app->hasTick_||app->paused_||app->lifecycle_!=LuaRuntimeState::Foreground)return;

    uint32_t elapsedUs=0;
    if(!app->callGlobal("on_tick",kTickHardBudgetMs,kTickHookCalls,&elapsedUs)) {
        lv_timer_pause(t);
        app->quarantine("on_tick");
        EventLog::instance().addTransient("WARN","LUA_TICK_PAUSED",app->id());
        return;
    }

    app->lastTickUs_=elapsedUs;
    if(elapsedUs>app->maxTickUs_)app->maxTickUs_=elapsedUs;

    if(elapsedUs>luaTickSoftBudgetUs(app->requestedTickMs_)) {
        if(app->slowTickStreak_<255)++app->slowTickStreak_;
        app->fastTickStreak_=0;
        const uint32_t newPeriod=luaTickShouldBackoff(app->slowTickStreak_)
            ? luaTickBackoff(app->requestedTickMs_,app->slowTickStreak_)
            : luaTickPeriod(app->requestedTickMs_,app->tickPenaltyMs_);
        app->tickPenaltyMs_=newPeriod;
        lv_timer_set_period(t,newPeriod);
        if(app->slowTickStreak_==1||app->slowTickStreak_==3) {
            EventLog::instance().addTransient(
                "WARN","LUA_TICK_SLOW",
                String(app->id())+": "+String(elapsedUs)+" us, period="+String(newPeriod)+" ms"
            );
        }
    } else {
        app->slowTickStreak_=0;
        if(++app->fastTickStreak_>=5) {
            app->fastTickStreak_=0;
            app->tickPenaltyMs_=0;
            lv_timer_set_period(t,app->requestedTickMs_);
        }
    }
}

bool LuaApp::initializeScript(const LuaScriptBuffer& script) {
    lifecycle_=LuaRuntimeState::Opening;
    lv_obj_clean(root_); // Remove only this app's loading placeholder once.
    memory_={};
    L_=lua_newstate(luaAppAlloc,&memory_,0);
    if(!L_) {
        EventLog::instance().addTransient("ERROR","LUA_MEMORY",id_);
        Serial.printf("[LUA] state allocation failed: %s\n",id_.c_str());
        close();
        return false;
    }

    openAppLibraries(L_);
    lua_pushlightuserdata(L_,this);
    lua_setfield(L_,LUA_REGISTRYINDEX,kRegistryKey);
    registerBridge(L_);
    if(luaL_loadbuffer(L_,kDeclarativeUi,sizeof(kDeclarativeUi)-1,"@firmware/ui-v3")!=LUA_OK||
       protectedCall(L_,0,1,10,100)!=LUA_OK){close();return false;}
    lua_setfield(L_,LUA_REGISTRYINDEX,"hp.v3.ui.step");
    if(luaL_loadbuffer(L_,kDataSubscriptions,sizeof(kDataSubscriptions)-1,"@firmware/data-v1")!=LUA_OK||
       protectedCall(L_,0,1,10,100)!=LUA_OK){close();return false;}
    lua_setfield(L_,LUA_REGISTRYINDEX,"hp.v3.data");

    if(!script.length()) {
        EventLog::instance().addTransient("ERROR","LUA_LOAD",id_+": script missing");
        close();
        return false;
    }

    const uint32_t compileStarted=micros();
    const int loaded=luaL_loadbuffer(L_,script.c_str(),script.length(),id_.c_str());
    UiPerformanceMonitor::instance().luaCompile(micros()-compileStarted);
    if(loaded!=LUA_OK||
       protectedCall(L_,0,0,kOpenCallbackMs,kOpenHookCalls)!=LUA_OK) {
        EventLog::instance().addTransient("ERROR","LUA_LOAD",id_+": script rejected");
        Serial.printf("[LUA] load failed: %s\n",id_.c_str());
        lua_pop(L_,1);
        close();
        return false;
    }

    return true;
}
bool LuaApp::queuePreparation(lv_obj_t* parent,bool settings){
    close();openStartedUs_=micros();settingsState_=settings;
    lifecycle_=LuaRuntimeState::Preparing;IdleService::instance().setContext(IdleContext::Normal);
    lv_obj_clean(parent);
    root_=lv_obj_create(parent);lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));
    lv_obj_set_style_bg_opa(root_,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(root_,0,0);
    auto* loading=lv_label_create(root_);lv_label_set_text(loading,"App wird vorbereitet ...");lv_obj_center(loading);
    auto* request=new(std::nothrow) LuaScriptPreparation;
    if(!request){lifecycle_=LuaRuntimeState::Error;lv_label_set_text(loading,"App konnte nicht vorbereitet werden: Speicher erschöpft");return false;}
    preparation_.reset(request);request->id=id_;request->folder=packageFolder_;
    request->appFs=packageAppFs_;request->usb=packageUsb_;request->usbGeneration=packageUsbGeneration_;request->settings=settings;
    if(defaultScript_)request->fallback=defaultScript_;
    if(!LuaScriptPreparation::enqueue(preparation_)){
        preparation_.reset();lifecycle_=LuaRuntimeState::Error;
        lv_label_set_text(loading,"App konnte nicht vorbereitet werden: Warteschlange voll");return false;
    }
    return true;
}

bool LuaApp::setTickInterval(uint32_t ms){
    if(!luaTickIntervalValid(ms)||paused_||faulted_||(lifecycle_!=LuaRuntimeState::Foreground&&lifecycle_!=LuaRuntimeState::Opening))return false;
    requestedTickMs_=ms;
    if(timer_)lv_timer_set_period(timer_,luaTickPeriod(ms,tickPenaltyMs_));
    return true;
}
void LuaApp::serviceUi(){
    if(preparation_){
        if(!preparation_->ready.load(std::memory_order_acquire))return;
        auto prepared=std::move(preparation_);
        auto* parent=root_?lv_obj_get_parent(root_):nullptr;
        if(prepared->error||!initializeScript(prepared->script)||
           (hasGlobal("on_open")&&!callGlobal("on_open",kOpenCallbackMs,kOpenHookCalls))){
            close();lifecycle_=LuaRuntimeState::Error;
            if(parent){root_=lv_obj_create(parent);lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));
                auto* error=lv_label_create(root_);lv_label_set_text_fmt(error,"App konnte nicht geladen werden:\n%s",prepared->error?prepared->error:"lua_init_failed");lv_obj_center(error);}
            return;
        }
        hasTick_=hasGlobal("on_tick");if(hasTick_)timer_=lv_timer_create(tickCb,requestedTickMs_,this);
        if(!settingsState_&&backgroundPolicy_.priority!=AppBackgroundPriority::None)AppBackgroundScheduler::instance().request(id(),backgroundPolicy_);
        uiBuilding_=true;
    }
    if(!L_||paused_||faulted_||(lifecycle_!=LuaRuntimeState::Foreground&&lifecycle_!=LuaRuntimeState::Opening))return;
    lua_getfield(L_,LUA_REGISTRYINDEX,"hp.v3.data");lua_getfield(L_,-1,"step");lua_remove(L_,-2);
    if(protectedCall(L_,0,0,5,50)!=LUA_OK){lua_pop(L_,1);quarantine("data.step");return;}
    lua_getfield(L_,LUA_REGISTRYINDEX,"hp.v3.ui.step");
    uiWorkPending_=false;
    if(protectedCall(L_,0,2,5,50)!=LUA_OK){lua_pop(L_,1);quarantine("ui.step");uiBuilding_=false;return;}
    const bool ready=lua_toboolean(L_,-2),meaningful=lua_toboolean(L_,-1);lua_pop(L_,2);
    if(uiBuilding_&&meaningful&&!meaningfulRecorded_){meaningfulRecorded_=true;UiPerformanceMonitor::instance().navigationMeaningful();}
    if(!ready)uiWorkPending_=true;
    if(uiBuilding_&&ready){
        uiBuilding_=false;lifecycle_=LuaRuntimeState::Foreground;
        UiAmbientAnimationService::instance().uiStateChanged();
        if(pendingLaunch_)launchArgumentsChanged();
        if(faulted_)return;
        UiPerformanceMonitor::instance().appBuild(micros()-openStartedUs_);UiPerformanceMonitor::instance().navigationBuilt();
    }
}

void LuaApp::launchArgumentsChanged(){
    if(lifecycle_==LuaRuntimeState::Preparing||lifecycle_==LuaRuntimeState::Opening){pendingLaunch_=true;return;}
    pendingLaunch_=false;
    if(L_&&!paused_&&!faulted_&&lifecycle_==LuaRuntimeState::Foreground&&hasGlobal("on_launch")&&
       !callGlobal("on_launch",kDefaultCallbackMs,kDefaultHookCalls))quarantine("on_launch");
}
void LuaApp::open(lv_obj_t* parent) { queuePreparation(parent,false); }
bool LuaApp::openSettings(lv_obj_t* parent) {
    return hasSettings()&&queuePreparation(parent,true);
}

void LuaApp::close() {
    VideoService::instance().close(id());
    if(preparation_){preparation_->cancelled.store(true,std::memory_order_release);preparation_.reset();}
    uiBuilding_=false;uiWorkPending_=false;meaningfulRecorded_=false;pendingLaunch_=false;
    lifecycle_=LuaRuntimeState::Closing;
    eventFilter_.reset();
    SecurityService::instance().lockSettings();
    LuaJobService::instance().cancelOwner(id());
    AppBackgroundScheduler::instance().remove(id());
    if(timer_) {
        lv_timer_delete(timer_);
        timer_=nullptr;
    }

    if(!faulted_&&L_&&hasGlobal("on_close"))
        callGlobal("on_close",kDefaultCallbackMs,kDefaultHookCalls);

    if(L_) {
        if(!faulted_&&hasGlobal("on_destroy"))callGlobal("on_destroy",kDefaultCallbackMs,kDefaultHookCalls);
        lua_close(L_);
        L_=nullptr;
    }

    if(root_&&lv_obj_is_valid(root_))lv_obj_delete(root_);
    root_=nullptr;
    hasTick_=false;
    requestedTickMs_=kLuaDefaultTickMs;
    paused_=false;
    faulted_=false;
    slowTickStreak_=0;
    tickPenaltyMs_=0;
    fastTickStreak_=0;
    lastTickUs_=0;
    maxTickUs_=0;
    for(auto* name:ownedNames_)free(name);
    ownedNames_.clear();
    for(auto* binding:bindings_){free(binding->name);delete binding;}
    bindings_.clear();
    uiObjects_=0;
    nativeBytes_=0;
    lifecycle_=LuaRuntimeState::Evicted;
    SecurityService::instance().lockSettings();
    LuaJobService::instance().cancelOwner(id());
    settingsState_=false;lifecycle_=LuaRuntimeState::Evicted;
    AppBackgroundScheduler::instance().remove(id());
}

}
