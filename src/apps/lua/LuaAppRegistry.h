#pragma once
#include <Arduino.h>
#include "../../core/AppPermissions.h"
#include "../../core/AppLifecycleCore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
namespace fs { class FS; }
namespace hp {
class LuaApp;
struct LuaAppManifest;
class LuaAppRegistry {
public:
    static LuaAppRegistry& instance();
    size_t discoverAndRegister(); // Legacy full rescan retained for rollback recovery.
    // UI task only, for an already-validated and atomically activated package.
    bool registerValidatedApp(const LuaAppManifest& manifest, const String& folder = String(), bool appFs = true, bool usb = false, uint32_t usbGeneration = 0);
    bool refreshUsbPackages(); // UI task only: close/remove stale removable owners.
    // UI only: asynchronously collect validated app metadata, then commit on the
    // LVGL/UI task. The worker MUST NOT call AppManager or create LVGL objects.
    bool requestBackgroundScan();
    bool applyBackgroundScan(); // true iff pending scan results were consumed
    bool scanBusy() const;
    // Guard live-package filesystem mutations against the background scanner.
    bool beginPackageMutation(uint32_t timeoutMs = 15000);
    bool beginPackageRead(uint32_t timeoutMs = 0);
    void endPackageRead();
    void endPackageMutation();
    // HTTP may delete files, but registration removal is always done in UI.
    bool queueUnregister(const char* id);
    bool unregisterApp(const char* id);
    // UI task only: remove a Lua registration even if AppManager and this
    // registry became inconsistent after an interrupted install.
    bool forceRemoveRegistration(const char* id);
    bool owns(const char* id) const;
    size_t dynamicCount() const { return count_; }
private:
    static constexpr size_t kMaxDynamicApps = 20;
    struct Discovered {
        String id, name, icon, version;
        String folder;
        bool appFs = true, settings = false, separateSettings = false;
        bool usb = false;
        bool webSettings = false;
        uint32_t usbGeneration = 0;
        bool enabled = true;
        AppPermissions permissions = 0;
        AppBackgroundPolicy background;
    };
    static void scanTask(void* arg);
    void runBackgroundScan();
    bool registerDiscovered(const Discovered& item);
    TaskHandle_t scanTask_ = nullptr;
    SemaphoreHandle_t scanMutex_ = nullptr;
    SemaphoreHandle_t packageIoMutex_ = nullptr;
    String pendingUnregister_[kMaxDynamicApps]{};
    size_t pendingUnregisterCount_ = 0;
    Discovered scanning_[kMaxDynamicApps]{}; // worker-owned until completion
    Discovered ready_[kMaxDynamicApps]{};    // protected by scanMutex_
    size_t scanningCount_ = 0;
    size_t readyCount_ = 0;
    uint32_t requestedGeneration_ = 0;
    uint32_t readyGeneration_ = 0;
    bool scanRunning_ = false;
    bool readyPending_ = false;
    bool scanRoot(fs::FS& fs, const String& root, bool appFs, uint8_t depth = 0, bool usb = false);
    bool tryRegisterFolder(fs::FS& fs, const String& folderPath, const String& fallbackId, bool appFs, bool usb = false);
    uint32_t observedUsbGeneration_ = 0;
    bool observedSdAvailable_ = false;
    bool validId(const String& id) const;
    LuaApp* apps_[kMaxDynamicApps]{};
    size_t count_ = 0;
};
}
