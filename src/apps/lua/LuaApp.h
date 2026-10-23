#pragma once
#include "../../core/AppManager.h"
#include <vector>
#include <memory>
#include <freertos/task.h>
#include "LuaEventFilter.h"
#include "LuaTickPolicy.h"

struct lua_State;

namespace hp {
class LuaScriptBuffer;
struct LuaScriptPreparation;

class LuaApp : public IApp {
public:
    LuaApp(
        const char* id,
        const char* name,
        const char* icon,
        const char* defaultScript = nullptr,
        bool enabledByDefault = false,
        const char* version = ""
    )
        : id_(id ? id : ""),
          name_(name ? name : ""),
          icon_(icon ? icon : LV_SYMBOL_HOME),
          defaultScript_(defaultScript),
          enabledByDefault_(enabledByDefault),
          version_(version ? version : "") {}

    const char* id() const override { return id_.c_str(); }
    const char* name() const override { return name_.c_str(); }
    const char* icon() const override { return icon_.c_str(); }
    const char* version() const override { return version_.c_str(); }
    bool userInstallable() const override { return true; }
    bool enabledByDefault() const override { return enabledByDefault_; }
    AppKind kind() const override { return AppKind::Lua; }
    AppPermissions permissions() const override { return permissions_; }
    bool allows(AppPermission permission) const { return (permissions_ & perm(permission)) != 0; }
    static LuaApp* self(::lua_State* L);
    static bool dispatchEvents(bool allowBackground=true);
    static bool dispatchBackground();
    static void bindBackgroundUiTask(){backgroundUiTask_=xTaskGetCurrentTaskHandle();}
    bool runBackground(const char* event=nullptr,uint32_t job=0,uint32_t* elapsedUs=nullptr);

    bool reserveUiObjects(size_t count) {
        if(count>1024-uiObjects_){++budgetFailures_;return false;}
        uiObjects_+=count;if(uiObjects_>uiObjectsPeak_)uiObjectsPeak_=uiObjects_;return true;
    }
    void releaseUiObjects(size_t count) { uiObjects_-=count>uiObjects_?uiObjects_:count; }
    bool reserveNativeBytes(size_t bytes) {
        if(bytes>512*1024-nativeBytes_){++budgetFailures_;return false;}
        nativeBytes_+=bytes;if(nativeBytes_>nativeBytesPeak_)nativeBytesPeak_=nativeBytes_;return true;
    }
    void releaseNativeBytes(size_t bytes) { nativeBytes_-=bytes>nativeBytes_?nativeBytes_:bytes; }
    size_t nativeBytesUsed()const{return nativeBytes_;}
    size_t nativeBytesPeak()const{return nativeBytesPeak_;}
    size_t uiObjectsUsed()const{return uiObjects_;}
    size_t uiObjectsPeak()const{return uiObjectsPeak_;}
    uint32_t budgetFailures()const{return budgetFailures_;}

    const String& assetPackageFolder() const { return packageFolder_; }
    bool assetPackageOnAppFs() const { return packageAppFs_; }
    bool assetPackageOnUsb() const { return packageUsb_; }
    uint32_t assetUsbGeneration() const { return packageUsbGeneration_; }

    void pause();
    void resume();
    bool setTickInterval(uint32_t ms);
    bool suspendTo(lv_obj_t* parking);
    bool resumeTo(lv_obj_t* parent);
    void evict();

    LuaRuntimeState lifecycle()const{return lifecycle_;}
    bool settingsState()const{return settingsState_;}
    void serviceUi(); // Called only by the guarded UiShell orchestration task.
    void requestUiWork(){uiWorkPending_=true;}
    bool uiWorkPending()const{return uiBuilding_||uiWorkPending_;}
    bool callUiEvent(const char* name,const char* value,int32_t index=0,int32_t count=0);
    bool opening()const{return preparation_||uiBuilding_;}
    bool failed()const{return faulted_||lifecycle_==LuaRuntimeState::Error;}
    void setLaunchArguments(const String& arguments)override{launchArguments_=arguments;}
    void launchArgumentsChanged()override;
    const String& launchArguments()const{return launchArguments_;}
    LuaEventFilter& eventFilter(){return eventFilter_;}
    bool uiAnimationEligible()const{
        return lifecycle_==LuaRuntimeState::Foreground &&
               !paused_ && !faulted_ && !settingsState_;
    }

    void configureBackground(const AppBackgroundPolicy& policy){backgroundPolicy_=policy;}

    bool begin() override { return id_.length() && name_.length(); }
    bool hasSettings() const override;
    const char* webPath() const override { return webPath_.c_str(); }
    void configureWebSettings(bool available) { webPath_=available ? String("/apps/")+id()+"/" : String(); }
    void open(lv_obj_t* parent) override;
    bool openSettings(lv_obj_t* parent) override;
    void close() override;

    lv_obj_t* screenRoot() const { return root_; }
    void trackOwnedName(char* name) { ownedNames_.push_back(name); }
    static void eventTrampoline(lv_event_t* e);
    struct EventBinding { LuaApp* app; char* name; };
    void bindEvent(lv_obj_t* object, lv_event_code_t code, const char* name);
    struct MemoryBudget { size_t used = 0; size_t peak = 0; };
    static constexpr size_t kMemoryLimit = 1024 * 1024;
    size_t memoryUsed() const { return memory_.used; }
    size_t memoryPeak() const { return memory_.peak; }

    void configurePackage(const String& folder, bool appFs, bool settings, bool separate, AppPermissions permissions = 0, bool usb = false, uint32_t usbGeneration = 0) {
        packageFolder_=folder;packageAppFs_=appFs;hasSettingsCache_=settings?1:0;separateSettings_=separate;
        packageUsb_=usb;packageUsbGeneration_=usbGeneration;
        permissions_ = permissions;
    }

private:
    static TaskHandle_t backgroundUiTask_;
    static void tickCb(lv_timer_t* t);
    bool callGlobal(const char* fn, uint32_t maxMs = 30,
                    int maxHookCalls = 250,
                    uint32_t* elapsedUs = nullptr);
    bool hasGlobal(const char* fn) const;
    bool queuePreparation(lv_obj_t* parent,bool settings);
    bool initializeScript(const LuaScriptBuffer&);
    std::shared_ptr<LuaScriptPreparation> preparation_;
    bool uiBuilding_=false;
    uint32_t openStartedUs_=0;

    String id_;
    String name_;
    String icon_;
    String version_;
    const char* defaultScript_ = nullptr;
    bool enabledByDefault_ = false;

    mutable int8_t hasSettingsCache_ = -1;
    String webPath_;
    String packageFolder_;
    bool packageAppFs_ = true;
    bool packageUsb_ = false;
    uint32_t packageUsbGeneration_ = 0;
    bool separateSettings_ = false;
    ::lua_State* L_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    bool hasTick_ = false;
    uint32_t requestedTickMs_=kLuaDefaultTickMs;
    uint8_t slowTickStreak_ = 0;
    uint32_t tickPenaltyMs_=0;
    uint8_t fastTickStreak_ = 0;
    uint32_t lastTickUs_ = 0;
    uint32_t maxTickUs_ = 0;
    std::vector<char*> ownedNames_;
    std::vector<EventBinding*> bindings_;
    MemoryBudget memory_;
    AppPermissions permissions_ = 0;
    String launchArguments_="{}";
    LuaEventFilter eventFilter_;
    bool paused_ = false;
    bool faulted_ = false;
    void quarantine(const char* callback);
    bool settingsState_=false,pendingLaunch_=false;
    LuaRuntimeState lifecycle_=LuaRuntimeState::Evicted;
    AppBackgroundPolicy backgroundPolicy_;
    size_t uiObjects_ = 0;
    bool uiWorkPending_=false;
    bool meaningfulRecorded_=false;
    size_t nativeBytes_ = 0;
    size_t nativeBytesPeak_=0,uiObjectsPeak_=0;
    uint32_t budgetFailures_=0;
};

}
