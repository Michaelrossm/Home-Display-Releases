#pragma once
#include "AppLifecycleCore.h"
#include "BackgroundScheduler.h"
#include <atomic>
#include <freertos/semphr.h>
namespace hp {
class AppBackgroundScheduler:public IBackgroundJob {
public:
    using Handler=void(*)(const char* owner); // Trusted native firmware only; never a Lua/LVGL callback.
    struct Diagnostics {uint32_t policies=0,executed=0,deferredHigh=0,deferredOffline=0,maxCallbackUs=0;};
    static AppBackgroundScheduler& instance();
    bool begin();
    bool request(const char* owner,const AppBackgroundPolicy& policy);
    bool registerNative(const char* owner,const AppBackgroundPolicy& policy,Handler handler,bool importantApproved=false);
    void remove(const char* owner);
    void setForegroundLoad(ForegroundLoad load){load_=load;}
    ForegroundLoad foregroundLoad()const{return load_.load();}
    void setForeground(const char* owner); // Existing guarded UI transition.
    bool isForeground(const char* owner);
    bool tryDiagnostics(Diagnostics& out);
    bool luaPending()const{return luaPending_.load(std::memory_order_acquire);}
    // Only the existing guarded UI dispatcher may consume this handoff.
    bool takeLuaOwner(char (&owner)[32]);
    void finishLua(uint32_t elapsedUs);
    void recordLuaEvent(uint32_t elapsedUs);
    bool ownerAllowed(const char* owner);
    bool hasPolicy(const char* owner);
    void backgroundTick()override;
    BackgroundJobClass backgroundClass()const override{return BackgroundJobClass::Blocking;}
    uint32_t backgroundIntervalMs()const override{return 60000;}
    const char* backgroundName()const override{return "app-background";}
private:
    struct Entry {char owner[32]{};AppBackgroundPolicy policy;Handler handler=nullptr;uint32_t due=0;};
    Entry entries_[32]{};char foreground_[32]{};SemaphoreHandle_t mutex_=nullptr;
    std::atomic<ForegroundLoad> load_{ForegroundLoad::Normal};Diagnostics diagnostics_;
    Entry pending_;bool pendingBusy_=false,pendingCancelled_=false;
    std::atomic<bool> luaPending_{false};
    static void executePending(void* context);
    bool store(const char* owner,const AppBackgroundPolicy& policy,Handler handler,bool approved);
};
}
