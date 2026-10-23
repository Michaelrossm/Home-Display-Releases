#include "LuaAppRegistry.h"
#include "LuaApp.h"
#include "LuaScriptReader.h"
#include "LuaAppManifest.h"
#include "../../core/AppManager.h"
#include "../../core/EventLog.h"
#include "../../services/StorageService.h"
#include "../../services/UsbStorageService.h"
#include "../../services/UsbAppFileSystem.h"
#include <ArduinoJson.h>
#include <FS.h>

namespace hp {
namespace {
String baseName(String path) {
    while (path.endsWith("/") && path.length() > 1) path.remove(path.length() - 1);
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}
const char* iconForName(String raw) {
    raw.toLowerCase();
    if (raw == "weather") return "@weather";
    if (raw == "printer" || raw == "3d_printer") return "@printer";
    if (raw == "power" || raw == "strom" || raw == "energy") return LV_SYMBOL_CHARGE;
    if (raw == "audio" || raw == "music" || raw == "radio") return "@radio";
    if (raw == "wifi" || raw == "network") return LV_SYMBOL_WIFI;
    if (raw == "settings") return LV_SYMBOL_SETTINGS;
    if (raw == "file") return LV_SYMBOL_FILE;
    if (raw == "directory" || raw == "folder") return LV_SYMBOL_DIRECTORY;
    if (raw == "plus") return LV_SYMBOL_PLUS;
    return LV_SYMBOL_HOME;
}
bool hasMainLua(fs::FS& fs, const String& folder) {
    File f = fs.open(folder + "/main.lua", FILE_READ);
    if (!f) return false;
    bool ok = !f.isDirectory() && f.size() > 0;
    f.close();
    return ok;
}
}

LuaAppRegistry& LuaAppRegistry::instance() {
    static LuaAppRegistry r;
    return r;
}

bool LuaAppRegistry::registerDiscovered(const Discovered& item) {
    if(item.usb&&!UsbStorageService::instance().validFiles(item.usbGeneration))return false;
    if (!validId(item.id) || !item.name.length() ||
        item.name.length() > 48 || count_ >= kMaxDynamicApps) return false;
    auto& manager = AppManager::instance();
    if (manager.find(item.id.c_str())) return false;
    auto* app = new LuaApp(item.id.c_str(), item.name.c_str(),
                           iconForName(item.icon), nullptr,
                           item.enabled, item.version.c_str());
    if (!app) return false;
    app->configurePackage(item.folder,item.appFs,item.settings,item.separateSettings,item.permissions,item.usb,item.usbGeneration);
    app->configureWebSettings(item.webSettings);
    app->configureBackground(item.background);
    if (!manager.registerApp(app)) { delete app; return false; }
    apps_[count_++] = app;
    return true;
}

bool LuaAppRegistry::registerValidatedApp(const LuaAppManifest& manifest, const String& folder, bool appFs, bool usb, uint32_t usbGeneration) {
    Discovered item;
    item.id = manifest.id;
    item.name = manifest.name;
    item.icon = manifest.icon;
    item.version = manifest.version;
    item.enabled = manifest.enabled;
    item.folder = folder.length() ? folder : String("/")+manifest.id;
    item.appFs = appFs;
    item.usb=usb;item.usbGeneration=usbGeneration;
    item.settings = manifest.settings;
    item.permissions = manifest.permissions;
    item.background = manifest.background;
    item.separateSettings = manifest.settings;
    return registerDiscovered(item);
}

bool LuaAppRegistry::requestBackgroundScan() {
    if (!scanMutex_) scanMutex_ = xSemaphoreCreateMutex();
    if (!packageIoMutex_) packageIoMutex_ = xSemaphoreCreateMutex();
    if (!scanMutex_ || !packageIoMutex_) return false;
    if (xSemaphoreTake(scanMutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    ++requestedGeneration_;
    if (!scanTask_) {
        BaseType_t result = xTaskCreate(scanTask, "lua-app-scan", 8192,
                                         this, tskIDLE_PRIORITY + 1,
                                         &scanTask_);
        if (result != pdPASS) {
            scanTask_ = nullptr;
            xSemaphoreGive(scanMutex_);
            return false;
        }
    }
    TaskHandle_t task = scanTask_;
    xSemaphoreGive(scanMutex_);
    xTaskNotifyGive(task);
    return true;
}

bool LuaAppRegistry::beginPackageMutation(uint32_t timeoutMs) {
    // The mutex is initialized by requestBackgroundScan() before the worker
    // starts. A package update before first UI scan may initialize it here.
    if (!packageIoMutex_) packageIoMutex_ = xSemaphoreCreateMutex();
    if (!packageIoMutex_ ||
        xSemaphoreTake(packageIoMutex_, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
        return false;
    if (scanMutex_ && xSemaphoreTake(scanMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        ++requestedGeneration_;
        readyPending_ = false;
        xSemaphoreGive(scanMutex_);
    }
    return true;
}

void LuaAppRegistry::endPackageMutation() {
    if (packageIoMutex_) xSemaphoreGive(packageIoMutex_);
}
bool LuaAppRegistry::beginPackageRead(uint32_t timeoutMs){
    return packageIoMutex_&&xSemaphoreTake(packageIoMutex_,pdMS_TO_TICKS(timeoutMs))==pdTRUE;
}
void LuaAppRegistry::endPackageRead(){if(packageIoMutex_)xSemaphoreGive(packageIoMutex_);}

bool LuaAppRegistry::queueUnregister(const char* id) {
    if (!id || !id[0] || !scanMutex_ ||
        xSemaphoreTake(scanMutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    bool ok = pendingUnregisterCount_ < kMaxDynamicApps;
    if (ok) {
        pendingUnregister_[pendingUnregisterCount_++] = id;
        ++requestedGeneration_;
        readyPending_ = false;
    }
    xSemaphoreGive(scanMutex_);
    return ok;
}

bool LuaAppRegistry::scanBusy() const {
    if (!scanMutex_) return false;
    if (xSemaphoreTake(scanMutex_, pdMS_TO_TICKS(1)) != pdTRUE) return true;
    const bool busy = scanRunning_;
    xSemaphoreGive(scanMutex_);
    return busy;
}

void LuaAppRegistry::scanTask(void* arg) {
    auto* self = static_cast<LuaAppRegistry*>(arg);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        self->runBackgroundScan();
    }
}

void LuaAppRegistry::runBackgroundScan() {
    if (xSemaphoreTake(scanMutex_, portMAX_DELAY) != pdTRUE) return;
    const uint32_t generation = requestedGeneration_;
    scanRunning_ = true;
    xSemaphoreGive(scanMutex_);

    // Package commit/deletion may replace live folders: never read them in
    // parallel, even if a scan was already queued before the transaction.
    if (xSemaphoreTake(packageIoMutex_, portMAX_DELAY) != pdTRUE) return;
    scanningCount_ = 0;
    const uint32_t usbGeneration=UsbStorageService::instance().generation();
    auto& st = StorageService::instance();
    if (st.appFsAvailable()) {
        if (auto* fs = st.fsFor(StorageClass::AppPackage)) {
            scanRoot(*fs, "/", true);
            scanRoot(*fs, "/apps", true);
            scanRoot(*fs, "/Apps/32", true);
        }
    }
    if (st.sdAvailable()) {
        if (auto* fs = st.fsFor(StorageClass::UserData)) {
            scanRoot(*fs, "/Apps/32", false);
            scanRoot(*fs, "/apps/32", false);
            scanRoot(*fs, "/apps", false);
        }
    }

    if(auto fs=usbAppFileSystemFor(usbGeneration)){
        scanRoot(*fs,"/Apps/32",false,0,true);scanRoot(*fs,"/apps",false,0,true);
    }
    for(size_t i=0;i<scanningCount_;++i)if(scanning_[i].usb)scanning_[i].usbGeneration=usbGeneration;

    xSemaphoreTake(scanMutex_, portMAX_DELAY);
    // If a new request arrived during the scan, discard the outdated result.
    if (generation == requestedGeneration_) {
        readyCount_ = scanningCount_;
        for (size_t i = 0; i < readyCount_; ++i) ready_[i] = scanning_[i];
        readyGeneration_ = generation;
        readyPending_ = true;
    } else {
        xTaskNotifyGive(scanTask_);
    }
    scanRunning_ = false;
    xSemaphoreGive(scanMutex_);
    xSemaphoreGive(packageIoMutex_);
}

bool LuaAppRegistry::applyBackgroundScan() {
    if (!scanMutex_) return false;

    // Never apply a scan result while an install/delete transaction owns the
    // package filesystem. A scan may have finished just before the mutation
    // started; without this guard its stale result can re-register the old app
    // between unregisterApp() and registerValidatedApp().
    if (packageIoMutex_ &&
        xSemaphoreTake(packageIoMutex_, 0) != pdTRUE) {
        return false;
    }

    if (xSemaphoreTake(scanMutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        if (packageIoMutex_) xSemaphoreGive(packageIoMutex_);
        return false;
    }

    String removed[kMaxDynamicApps];
    const size_t removals = pendingUnregisterCount_;
    for (size_t i = 0; i < removals; ++i) removed[i] = pendingUnregister_[i];
    pendingUnregisterCount_ = 0;

    const bool ready = readyPending_ && readyGeneration_ == requestedGeneration_;
    Discovered local[kMaxDynamicApps];
    const size_t total = ready ? readyCount_ : 0;
    for (size_t i = 0; i < total; ++i) local[i] = ready_[i];
    if (readyPending_) readyPending_ = false;
    xSemaphoreGive(scanMutex_);

    // This method is called by UiShell holding its LVGL lock. Never close
    // app UI or free a Lua app from the HTTP or worker task.
    bool changed = false;
    for (size_t i = 0; i < removals; ++i)
        if (unregisterApp(removed[i].c_str())) changed = true;

    size_t added = 0;
    for (size_t i = 0; i < total; ++i)
        if (registerDiscovered(local[i])) ++added;
    if (added) {
        changed = true;
        EventLog::instance().add("INFO", "LUA_APP_RESCAN", String(added) + " new");
    }

    if (packageIoMutex_) xSemaphoreGive(packageIoMutex_);
    return changed;
}

bool LuaAppRegistry::validId(const String& id) const {
    if (!id.length() || id.length() > 31) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    }
    return true;
}

bool LuaAppRegistry::tryRegisterFolder(fs::FS& fs, const String& folderPath, const String& fallbackId, bool appFs, bool usb) {
    const auto usbGeneration=UsbStorageService::instance().generation();
    if (!hasMainLua(fs, folderPath)) return false;

    String id = fallbackId;
    String name;
    String icon = fallbackId;
    String version;
    bool enabledByDefault = true;
    bool settings = false, separateSettings = false;

    LuaAppManifest strictManifest;
    String manifestError;
    bool strictLoaded = false;

    const String appJson = folderPath + "/app.json";
    const String legacyJson = folderPath + "/manifest.json";
    if (fs.exists(appJson)) {
        strictLoaded = LuaAppManifestParser::load(fs, appJson, strictManifest, manifestError);
    } else if (fs.exists(legacyJson)) {
        strictLoaded = LuaAppManifestParser::load(fs, legacyJson, strictManifest, manifestError);
    }

    if (strictLoaded) {
        id = strictManifest.id;
        name = strictManifest.name;
        icon = strictManifest.icon;
        version = strictManifest.version;
        enabledByDefault = strictManifest.enabled;
        if(strictManifest.settings) {
            File f=fs.open(folderPath+"/settings.lua",FILE_READ);
            separateSettings=f&&!f.isDirectory()&&f.size()>0&&f.size()<=196608;
            if(f)f.close();
            settings=separateSettings;
            if(strictManifest.settingsEntryExplicit&&!separateSettings)return false;
        }
    } else {
        // V3 never executes an unvalidated/legacy package. Preserve its files
        // and data; the diagnostic tells the user which package needs updating.
        EventLog::instance().addTransient("WARN","LUA_MANIFEST_INVALID",
            folderPath+":"+manifestError);
        return false;
    }
    id.trim(); name.trim();
    if (!validId(id) || !name.length() || name.length() > 48 ||
        id != fallbackId) {
        // The directory name is the canonical identity and LuaApp loads by ID.
        // Without this check, the scanner can show an app whose main.lua cannot load.

        EventLog::instance().add("WARN", "LUA_APP_INVALID", folderPath);
        return false;
    }

    if(usb&&!UsbStorageService::instance().validFiles(usbGeneration))return false;
    // The worker only reads files and collects metadata. Dynamic registration
    // and app ownership remain on the UI task, never on this worker.
    if (scanTask_ && xTaskGetCurrentTaskHandle() == scanTask_) {
        if (scanningCount_ >= kMaxDynamicApps) return false;
        for (size_t i = 0; i < scanningCount_; ++i)
            if (scanning_[i].id == id) return false;
        auto& result = scanning_[scanningCount_++];
        result.id = id;
        result.name = name;
        result.icon = icon;
        result.version = version;
        result.enabled = enabledByDefault;
        result.folder=folderPath;result.appFs=appFs;result.settings=settings;result.separateSettings=separateSettings;
        result.usb=usb;result.usbGeneration=usbGeneration;
        result.webSettings=fs.exists((folderPath+"/web_settings.json").c_str());
        result.permissions = strictLoaded ? strictManifest.permissions : 0;
        result.background = strictLoaded ? strictManifest.background : AppBackgroundPolicy{};
        return true;
    }

    auto& manager = AppManager::instance();
    if (manager.find(id.c_str())) return false; // id already registered
    if (count_ >= kMaxDynamicApps) {
        EventLog::instance().add("WARN", "LUA_APP_LIMIT", id);
        return false;
    }

    auto* app = new LuaApp(id.c_str(), name.c_str(), iconForName(icon), nullptr, enabledByDefault, version.c_str());
    if (!app) return false;
    app->configurePackage(folderPath,appFs,settings,separateSettings,strictLoaded ? strictManifest.permissions : 0,usb,usbGeneration);
    app->configureWebSettings(fs.exists((folderPath+"/web_settings.json").c_str()));
    app->configureBackground(strictLoaded ? strictManifest.background : AppBackgroundPolicy{});
    if (!manager.registerApp(app)) {
        delete app;
        EventLog::instance().add("WARN", "LUA_APP_REGISTER", id);
        return false;
    }

    apps_[count_++] = app;
    EventLog::instance().add("INFO", "LUA_APP_FOUND",
        id + String(appFs ? " AppFS " : " SD ") + folderPath);
    return true;
}

bool LuaAppRegistry::scanRoot(fs::FS& fs, const String& root, bool appFs, uint8_t depth, bool usb) {
    if (depth > 2) return false;
    File dir = fs.open(root);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }

    bool found = false;
    File e = dir.openNextFile();
    while (e) {
        bool isDir = e.isDirectory();
        String path = e.path();
        e.close();

        if (isDir) {
            String folder = baseName(path);
            if (folder.length() &&
                folder != "installer_stage" &&
                folder != "installer_backup" &&
                !folder.startsWith(".") &&
                folder != "webapps" &&
                folder != "." &&
                folder != "..") {
                if (tryRegisterFolder(fs, path, folder, appFs,usb)) found = true;
                // No recursive discovery: only direct child package folders are apps.
                // The loader uses these canonical roots, not arbitrary subfolders.
            }
        }
        e = dir.openNextFile();
    }
    dir.close();
    return found;
}

bool LuaAppRegistry::owns(const char* id) const {
    if (!id || !id[0]) return false;
    for (size_t i = 0; i < count_; ++i) {
        if (apps_[i] && strcmp(apps_[i]->id(), id) == 0) return true;
    }
    return false;
}

bool LuaAppRegistry::unregisterApp(const char* id) {
    if (!id || !id[0]) return false;

    for (size_t i = 0; i < count_; ++i) {
        auto* app = apps_[i];
        if (!app || strcmp(app->id(), id) != 0) continue;

        AppManager::instance().unregisterApp(id);
        delete app;

        for (size_t j = i + 1; j < count_; ++j) {
            apps_[j - 1] = apps_[j];
        }
        apps_[--count_] = nullptr;

        EventLog::instance().add("INFO", "LUA_APP_UNREGISTER", id);
        return true;
    }

    return false;
}

bool LuaAppRegistry::forceRemoveRegistration(const char* id) {
    if (!id || !id[0]) return false;

    // Normal owned path first: removes from both AppManager and registry array.
    if (unregisterApp(id)) return true;

    // Recovery path for an interrupted/failed install:
    // AppManager may still own a LuaApp which is no longer present in apps_[].
    auto& manager = AppManager::instance();
    IApp* ghost = manager.find(id);
    if (!ghost) return true;
    if (ghost->kind() != AppKind::Lua) return false;
    if (manager.current() == ghost) return false;

    if (!manager.unregisterApp(id)) return false;
    delete ghost;

    EventLog::instance().add(
        "WARN", "LUA_APP_GHOST_REMOVED",
        String(id) + " removed from AppManager"
    );
    return true;
}

bool LuaAppRegistry::refreshUsbPackages(){
    auto& usb=UsbStorageService::instance();const auto generation=usb.generation();
    const bool sd=StorageService::instance().sdAvailable();
    if(generation==observedUsbGeneration_&&sd==observedSdAvailable_)return false;
    if(packageIoMutex_&&xSemaphoreTake(packageIoMutex_,0)!=pdTRUE)return false;
    observedUsbGeneration_=generation;observedSdAvailable_=sd;
    for(size_t i=0;i<count_;){
        auto* app=apps_[i];
        const bool staleUsb=app->assetPackageOnUsb()&&!usb.validFiles(app->assetUsbGeneration());
        const bool staleSd=!sd&&!app->assetPackageOnUsb()&&!app->assetPackageOnAppFs();
        if(staleUsb||staleSd){
            const String id=app->id();unregisterApp(id.c_str());
        }else ++i;
    }
    if(packageIoMutex_)xSemaphoreGive(packageIoMutex_);
    requestBackgroundScan();return true;
}

size_t LuaAppRegistry::discoverAndRegister() {
    const size_t before = count_;
    auto& st = StorageService::instance();

    if (st.appFsAvailable()) {
        if (auto* fs = st.fsFor(StorageClass::AppPackage)) {
            scanRoot(*fs, "/", true);
            scanRoot(*fs, "/apps", true);
            scanRoot(*fs, "/Apps/32", true);
        }
    }
    if (st.sdAvailable()) {
        if (auto* fs = st.fsFor(StorageClass::UserData)) {
            scanRoot(*fs, "/Apps/32", false);
            scanRoot(*fs, "/apps/32", false);
            scanRoot(*fs, "/apps", false);
        }
    }

    const auto generation=UsbStorageService::instance().generation();
    if(auto fs=usbAppFileSystemFor(generation)){
        scanRoot(*fs,"/Apps/32",false,0,true);scanRoot(*fs,"/apps",false,0,true);
    }
    const size_t added = count_ - before;
    if (added) EventLog::instance().add("INFO", "LUA_APP_RESCAN", String(added) + " new");
    return added;
}
}
