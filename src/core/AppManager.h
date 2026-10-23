#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "AppPermissions.h"
#include "AppLifecycleCore.h"
#include <atomic>

namespace hp {

enum class AppKind : uint8_t {
    System = 0,
    Native = 1,
    Lua = 2,
    Web = 3
};

class IApp {
public:
    virtual ~IApp() = default;
    virtual const char* id() const = 0;
    virtual const char* name() const = 0;
    virtual const char* icon() const = 0;
    virtual const char* version() const { return ""; }
    virtual AppPermissions permissions() const { return 0; }

    // Apps that may be enabled/disabled by the user.
    virtual bool userInstallable() const { return false; }
    // Launcher presence does not grant disable/uninstall privileges.
    virtual bool launcherVisible() const { return userInstallable(); }
    virtual bool enabledByDefault() const { return false; }
    virtual AppKind kind() const {
        return userInstallable() ? AppKind::Native : AppKind::System;
    }

    virtual bool begin() = 0;
    virtual bool hasSettings() const { return false; }
    virtual const char* webPath() const { return ""; }
    virtual void open(lv_obj_t* parent) = 0;
    virtual bool openSettings(lv_obj_t* parent) { (void)parent; return false; }
    virtual void close() = 0;
    virtual void setLaunchArguments(const String&) {}
    virtual void launchArgumentsChanged() {}
};

enum class AppRuntimeState : uint8_t {
    Ready,
    Opening,
    Open,
    Settings,
    Closed,
    Error,
    Suspended,
    Evicted
};

struct AppRuntimeStatus {
    AppRuntimeState state = AppRuntimeState::Ready;
    String detail;
    uint32_t changedAtMs = 0;
};

class AppManager {
public:
    static AppManager& instance();

    bool registerApp(IApp* app);
    bool unregisterApp(const char* id);
    IApp* find(const char* id) const;

    bool open(const char* id, lv_obj_t* parent);
    bool openWithArguments(const char* id,lv_obj_t* parent,const String& arguments);
    bool openSettings(const char* id, lv_obj_t* parent);
    void closeCurrent();
    void suspendCurrent(); // Home/app switch only, existing UI guard required.
    void serviceCurrentUi(); // Bounded Lua preparation/render steps under UI guard.
    struct LuaDiagnostics {uint32_t foreground=0,suspended=0,evicted=0,evictions=0;};
    LuaDiagnostics luaDiagnostics()const;

    bool isEnabled(const IApp* app) const;
    bool isEnabled(const char* id) const;
    bool setEnabled(const IApp* app, bool enabled, String* error=nullptr);
    bool setEnabled(const char* id, bool enabled);

    IApp* current() const { return current_; }
    AppRuntimeStatus runtimeStatus(const char* id) const;
    static const char* runtimeStateName(AppRuntimeState state);
    size_t count() const { return n_; }
    IApp* at(size_t i) const { return i < n_ ? apps_[i] : nullptr; }

private:
    static constexpr size_t MAX = 32;
    void setRuntime(IApp* app, AppRuntimeState state, const String& detail = "");
    IApp* apps_[MAX]{};
    AppRuntimeStatus runtime_[MAX]{};
    size_t n_ = 0;
    IApp* current_ = nullptr;
    SuspendedAppLru<IApp> suspended_;
    lv_obj_t* parking_=nullptr;
    uint32_t evictions_=0;
    std::atomic<bool> disabledCleanup_{false};
    String launchArguments_="{}";
    void evict(IApp* app);
    void trimSuspended();
};

}
