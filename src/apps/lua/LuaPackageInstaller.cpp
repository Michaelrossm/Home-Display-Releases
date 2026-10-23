#include "LuaPackageInstaller.h"
#include "LuaAppRegistry.h"
#include "LuaApp.h"
#include "LuaScriptReader.h"
#include "../../services/UsbStorageService.h"
#include "../../services/UsbAppFileSystem.h"
#include "LuaImagePolicy.h"
#include "../../services/ImageCodec.h"
#include "../../core/AppManager.h"
#include "../../core/EventLog.h"
#include "../../core/AppRegistry.h"
#include "../../ui/UiShell.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include "../../services/StorageService.h"
#include "../../services/ConfigService.h"
#include "../../services/WebSettingsSchemaValidator.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace hp {
namespace {
struct PendingCommit {
    String id;
    bool allowDowngrade = false;
    LuaInstallPlan plan;
    String error;
    SemaphoreHandle_t finished = nullptr;
    bool processing = false;
    bool success = false;
};
portMUX_TYPE pendingCommitMux = portMUX_INITIALIZER_UNLOCKED;
PendingCommit* pendingCommit = nullptr;

struct ValidationMemory { size_t used=0; };
void* luaValidationAlloc(void* context, void* ptr, size_t oldSize, size_t newSize) {
    auto& memory=*static_cast<ValidationMemory*>(context);
    const size_t previous=ptr?oldSize:0;
    if (newSize == 0) {
        if (ptr) heap_caps_free(ptr);
        memory.used-=previous;
        return nullptr;
    }

    if(newSize>1024*1024||memory.used-previous>1024*1024-newSize)return nullptr;
    if (!ptr) {
        void* p = heap_caps_malloc(
            newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p)
            p = heap_caps_malloc(newSize, MALLOC_CAP_8BIT);
        if(p)memory.used+=newSize;
        return p;
    }

    void* p = heap_caps_malloc(newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        p = heap_caps_malloc(newSize, MALLOC_CAP_8BIT);
    if(p){memcpy(p,ptr,std::min(oldSize,newSize));heap_caps_free(ptr);memory.used=memory.used-previous+newSize;}
    return p;
}

bool ensureFsDir(fs::FS& fs, const String& path) {
    if (fs.exists(path)) {
        File node = fs.open(path, FILE_READ);
        const bool ok = node && node.isDirectory();
        if (node) node.close();
        return ok;
    }
    return fs.mkdir(path);
}

bool copyFailure(String* detail,const char* reason,const String& path){if(detail)*detail=String(reason)+":"+path;return false;}
uint32_t copyChecksum(uint32_t value,const uint8_t* bytes,size_t size){for(size_t i=0;i<size;++i)value=(value^bytes[i])*16777619u;return value;}
bool copyFsFile(fs::FS& fs, const String& from, const String& to,String* detail=nullptr) {
    if(from==to)return copyFailure(detail,"copy_same_path",from);
    File in = fs.open(from, FILE_READ);
    if (!in || in.isDirectory()) {
        if (in) in.close();
        return copyFailure(detail,"copy_open_source",from);
    }

    const size_t expected=in.size();
    if(fs.exists(to)&&!fs.remove(to)){in.close();return copyFailure(detail,"copy_remove_destination",to);}
    File out = fs.open(to, FILE_WRITE);
    if (!out) {
        in.close();
        return copyFailure(detail,"copy_open_destination",to);
    }

    uint8_t buffer[1024];
    bool ok = true;
    size_t copied=0;uint32_t checksum=2166136261u;const char* reason="copy_size_mismatch";
    while (copied<expected) {
        const size_t got = in.read(buffer, std::min(sizeof(buffer),expected-copied));
        if (!got) { ok = false;reason="copy_short_read";break; }
        if (out.write(buffer, got) != got) { ok = false;reason="copy_short_write";break; }
        checksum=copyChecksum(checksum,buffer,got);
        copied+=got;
        delay(0);
    }
    out.flush();
    ok=ok&&copied==expected&&out.size()==expected;
    out.close();
    in.close();

    if(ok){File check=fs.open(to,FILE_READ);size_t verified=0;uint32_t actual=2166136261u;
        ok=check&&!check.isDirectory()&&check.size()==expected;
        while(ok&&verified<expected){const size_t got=check.read(buffer,std::min(sizeof(buffer),expected-verified));
            if(!got){ok=false;break;}actual=copyChecksum(actual,buffer,got);verified+=got;delay(0);}
        ok=ok&&verified==expected&&actual==checksum;check.close();if(!ok)reason="copy_verify_failed";}
    if (!ok){fs.remove(to);copyFailure(detail,reason,to);}
    return ok;
}

String assetName(const File& file){String name=file.name();const int slash=name.lastIndexOf('/');return slash>=0?name.substring(slash+1):name;}
void clearPackageAssets(fs::FS& fs,const String& dir) {
    File folder=fs.open(dir+"/assets",FILE_READ);if(!folder||!folder.isDirectory())return;
    std::vector<String> names;for(File file=folder.openNextFile();file;file=folder.openNextFile()){
        String name=assetName(file),base=name;if(base.endsWith(".part"))base.remove(base.length()-5);
        if(!file.isDirectory()&&LuaImagePolicy::validPath(std::string((String("assets/")+base).c_str())))names.push_back(name);file.close();}
    folder.close();for(const auto& name:names)fs.remove(dir+"/assets/"+name);fs.rmdir(dir+"/assets");
}
bool copyPackageAssets(fs::FS& fs,const String& from,const String& to,String* detail=nullptr) {
    if(!fs.exists(from+"/assets"))return true;File folder=fs.open(from+"/assets",FILE_READ);
    if(!folder||!folder.isDirectory())return copyFailure(detail,"asset_directory",from);
    std::vector<String> names;size_t count=0,total=0;for(File file=folder.openNextFile();file;file=folder.openNextFile()){
        const String name=assetName(file);const size_t size=file.size();const bool valid=!file.isDirectory()&&LuaImagePolicy::validPath(std::string((String("assets/")+name).c_str()));file.close();
        if(!valid||size>LuaImagePolicy::maxFileBytes||++count>LuaImagePolicy::maxAssets||(total+=size)>LuaImagePolicy::maxPackageBytes)return copyFailure(detail,"asset_policy_or_limit",name);
        names.push_back(name);
    }
    // Close the iterator before opening input/output files. FAT/AppFS has a
    // bounded handle pool shared with UI fonts and media readers.
    folder.close();if(!ensureFsDir(fs,to+"/assets"))return copyFailure(detail,"asset_destination_directory",to);
    for(const auto& name:names)if(!copyFsFile(fs,from+"/assets/"+name,to+"/assets/"+name,detail))return false;
    return true;
}
bool validatePackageAssets(fs::FS& fs,const String& dir) {
    if(!fs.exists(dir+"/assets"))return true;File folder=fs.open(dir+"/assets",FILE_READ);if(!folder||!folder.isDirectory())return false;
    size_t count=0,total=0;for(File file=folder.openNextFile();file;file=folder.openNextFile()){
        uint32_t width=0,height=0;const String name=assetName(file);const size_t size=file.size();
        if(file.isDirectory()||!LuaImagePolicy::validPath(std::string((String("assets/")+name).c_str()))||size>LuaImagePolicy::maxFileBytes||
           ++count>LuaImagePolicy::maxAssets||(total+=size)>LuaImagePolicy::maxPackageBytes||size<12)return false;
        std::unique_ptr<uint8_t,decltype(&free)> bytes(static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),free);
        if(!bytes||file.read(bytes.get(),size)!=size||!ImageCodec::dimensions(bytes.get(),size,width,height)||width>256||height>256)return false;file.close();
    }return true;
}
void clearPackageFiles(fs::FS& fs, const String& dir) {
    clearPackageAssets(fs,dir);
    fs.remove(dir + "/app.json");
    fs.remove(dir + "/manifest.json");
    fs.remove(dir + "/main.lua");
    fs.remove(dir + "/settings.lua");
    fs.remove(dir + "/web_settings.json");
    fs.remove(dir + "/app.json.part");
    fs.remove(dir + "/manifest.json.part");
    fs.remove(dir + "/main.lua.part");
    fs.remove(dir + "/settings.lua.part");
    fs.remove(dir + "/web_settings.json.part");
}

bool copyPackageFiles(fs::FS& fs, const String& fromDir, const String& toDir,String* detail=nullptr) {
    if(fromDir==toDir)return copyFailure(detail,"copy_same_directory",fromDir);
    if (!ensureFsDir(fs, toDir)) return copyFailure(detail,"package_destination_directory",toDir);

    const bool hasApp = fs.exists(fromDir + "/app.json");
    const bool hasLegacy = fs.exists(fromDir + "/manifest.json");
    const bool hasMain = fs.exists(fromDir + "/main.lua");
    if ((!hasApp && !hasLegacy) || !hasMain) return copyFailure(detail,"source_package_incomplete",fromDir);

    clearPackageFiles(fs, toDir);

    if (hasApp && !copyFsFile(fs, fromDir + "/app.json", toDir + "/app.json",detail))
        return false;
    if (hasLegacy &&
        !copyFsFile(fs, fromDir + "/manifest.json", toDir + "/manifest.json",detail))
        return false;
    if (!copyFsFile(fs, fromDir + "/main.lua", toDir + "/main.lua",detail))
        return false;
    if (fs.exists(fromDir + "/settings.lua") &&
        !copyFsFile(fs,fromDir+"/settings.lua",toDir+"/settings.lua",detail))return false;
    if (fs.exists(fromDir + "/web_settings.json") &&
        !copyFsFile(fs,fromDir+"/web_settings.json",toDir+"/web_settings.json",detail))return false;

    return copyPackageAssets(fs,fromDir,toDir,detail);
}
}

LuaPackageInstaller& LuaPackageInstaller::instance() {
    static LuaPackageInstaller installer;
    return installer;
}

const char* LuaPackageInstaller::modeName(LuaInstallMode mode) {
    switch (mode) {
        case LuaInstallMode::Install: return "install";
        case LuaInstallMode::Update: return "update";
        case LuaInstallMode::Reinstall: return "reinstall";
        case LuaInstallMode::Downgrade: return "downgrade";
    }
    return "unknown";
}

bool LuaPackageInstaller::resetStage(const char* rawId, String& error) {
    auto& registry=LuaAppRegistry::instance();
    if(!registry.beginPackageRead()){error="package_scan_busy";return false;}
    struct ReadGuard {LuaAppRegistry& registry;~ReadGuard(){registry.endPackageRead();}} read{registry};
    String id = rawId ? rawId : "";
    if (!LuaAppManifestParser::validId(id)) {
        error = "invalid_id";
        return false;
    }

    auto& storage = StorageService::instance();
    // Pin one target through upload, validation, activation and rollback.
    // Updates stay at their current location; never silently fall back to flash.
    targetId_ = id; targetLive_ = String("/") + id;
    const auto selected=storage.newAppStorage();
    const auto usbGeneration=UsbStorageService::instance().generation();
    auto usbSource=usbAppFileSystemFor(usbGeneration);
    targetSd_=selected==StorageClass::Media;targetUsb_=selected==StorageClass::UsbAppPackage;
    // Never replace an installed USB package on another medium while absent.
    if(auto* existing=AppManager::instance().find(id.c_str())){
        if(existing->kind()==AppKind::Lua&&static_cast<LuaApp*>(existing)->assetPackageOnUsb()&&
           !UsbStorageService::instance().available()){error="usb_app_unavailable";return false;}
    }
    size_t copies = 0;
    for (auto medium : {StorageClass::AppPackage,StorageClass::Media,StorageClass::UsbAppPackage}) {
        const bool sd=medium==StorageClass::Media,usb=medium==StorageClass::UsbAppPackage;
        auto* source = usb?usbSource.get():storage.fsFor(medium);
        if (!source) continue;
        // FAT is case-insensitive: do not count /Apps/32 and /apps/32 twice.
        const String roots[] = {"/", "/apps/", "/Apps/32/", "/apps/32/", "/Apps/"};
        for (size_t n = (sd||usb) ? 1 : 0; n < ((sd||usb) ? 3 : 5); ++n) {
            const auto& root = roots[n];
            const String folder = root + id;
            if (source->exists(folder + "/main.lua")) {
                ++copies; targetSd_ = sd; targetUsb_=usb;targetLive_ = folder;
            }
        }
    }
    if (copies > 1) { error = "duplicate_package_locations"; targetId_ = ""; return false; }
    if (!copies && (targetSd_||targetUsb_)) targetLive_ = String("/Apps/32/") + id;
    targetUsbGeneration_=usbGeneration;
    targetUsbFs_=targetUsb_?usbAppFileSystemFor(targetUsbGeneration_):nullptr;
    auto* fs = stageFs(id.c_str());
    if (!fs) {
        error = targetUsb_?"usb_unavailable_or_changed":"appfs_unavailable";
        return false;
    }

    const String backup = backupDir(id);
    if (fs->exists(backup)) {
        const bool backupHasPackage =
            (fs->exists(backup + "/app.json") ||
             fs->exists(backup + "/manifest.json")) &&
            fs->exists(backup + "/main.lua");
        if (backupHasPackage) {
            error = "rollback_backup_present:" + backup;
            return false;
        }
        clearPackageFiles(*fs, backup);
        fs->rmdir(backup);
    }

    const String root = "/installer_stage";
    const String stage = stageDir(id);

    if (!fs->exists(root) && !fs->mkdir(root)) {
        error = "stage_root_mkdir";
        return false;
    }

    /*
     * Reuse the existing stage directory instead of requiring it to be
     * deleted. On this AppFS/LittleFS combination, directory deletion/rename
     * can fail even though the directory is still writable.
     *
     * Reset only known package members, including the bounded assets folder.
     */
    if (!fs->exists(stage)) {
        if (!fs->mkdir(stage)) {
            error = "stage_app_mkdir";
            return false;
        }
    } else {
        File existing = fs->open(stage, FILE_READ);
        const bool isDir = existing && existing.isDirectory();
        if (existing) existing.close();

        if (!isDir) {
            if (!fs->remove(stage) || !fs->mkdir(stage)) {
                error = "stage_path_not_directory:" + stage;
                return false;
            }
        }
    }

    // Ignore "not found". A following upload rewrites these paths anyway.
    clearPackageAssets(*fs,stage);
    fs->remove(stage + "/app.json");
    fs->remove(stage + "/manifest.json");
    fs->remove(stage + "/main.lua");
    fs->remove(stage + "/settings.lua");
    fs->remove(stage + "/web_settings.json");
    fs->remove(stage + "/app.json.part");
    fs->remove(stage + "/manifest.json.part");
    fs->remove(stage + "/main.lua.part");
    fs->remove(stage + "/settings.lua.part");
    fs->remove(stage + "/web_settings.json.part");

    EventLog::instance().add(
        "INFO", "LUA_STAGE_READY",
        id + " stage reused/reset"
    );

    return true;
}

bool LuaPackageInstaller::stagePath(
    const char* rawId,
    const char* rawFileName,
    String& outPath,
    String& error
) const {
    String id = rawId ? rawId : "";
    String fileName = rawFileName ? rawFileName : "";

    if (!LuaAppManifestParser::validId(id)) {
        error = "invalid_id";
        return false;
    }

    // New packages use app.json. manifest.json remains accepted as a legacy alias.
    if (!(fileName == "app.json" || fileName == "manifest.json" || fileName == "main.lua" || fileName == "settings.lua" || fileName == "web_settings.json" || LuaImagePolicy::validPath(std::string(fileName.c_str())))) {
        error = "invalid_package_file";
        return false;
    }

    outPath = stageDir(id) + "/" + fileName;
    return true;
}

bool LuaPackageInstaller::callbackExists(const LuaScriptBuffer& script, const char* name) const {
    const String a = String("function ") + name;
    const String b = String(name) + " = function";
    return script.indexOf(a.c_str()) >= 0 || script.indexOf(b.c_str()) >= 0;
}

bool LuaPackageInstaller::validateLuaSyntax(
    fs::FS& fs,
    const String& path,
    const LuaAppManifest& manifest,
    String& error
) {
    File f = fs.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        error = path.endsWith("/settings.lua") ? "settings_file_missing" : "main_missing";
        return false;
    }

    if (f.size() == 0 || f.size() > 196608) {
        f.close();
        error = path.endsWith("/settings.lua") ? "settings_size" : "main_size";
        return false;
    }

    LuaScriptBuffer script;
    const char* readError = nullptr;
    const bool readOk = readLuaScript(f, script, readError);
    f.close();
    if (!readOk) { error = readError; return false; }

    // Syntax validation is temporary and can require far more heap than the
    // script size itself. Prefer PSRAM so a web install does not fail merely
    // because internal RAM is fragmented by LVGL/network/audio tasks.
    ValidationMemory memory;
    lua_State* L = lua_newstate(luaValidationAlloc, &memory, 0);
    if (!L) {
        error = "lua_memory:validator_state";
        return false;
    }

    const int rc = luaL_loadbuffer(
        L, script.c_str(), script.length(), manifest.id.c_str());
    if (rc != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        if (rc == LUA_ERRMEM) {
            error = "lua_memory:syntax_validator";
        } else {
            error = "lua_syntax:";
            error += message ? message : "unknown";
        }
        lua_close(L);
        return false;
    }
    lua_close(L);

    if (manifest.settings && path.endsWith("/settings.lua") && !callbackExists(script,"on_open") &&
        !callbackExists(script, "on_settings") &&
        !callbackExists(script, "open_settings") &&
        !callbackExists(script, "settings")) {
        error = "settings_callback_missing";
        return false;
    }

    return true;
}

bool validateWebSettingsJson(fs::FS& fs, const String& path, String& error) {
    if (!fs.exists(path)) return true;
    File f = fs.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        error = "web_settings_open";
        return false;
    }
    if (f.size() == 0 || f.size() > WebSettingsSchemaValidator::kMaxBytes) {
        f.close();
        error = "web_settings_size";
        return false;
    }
    JsonDocument doc;
    const auto parse = deserializeJson(doc, f, DeserializationOption::NestingLimit(12));
    f.close();
    if (parse) {
        error = String("web_settings_json:") + parse.c_str();
        return false;
    }
    return WebSettingsSchemaValidator::validate(doc, error);
}

bool LuaPackageInstaller::validateStagedFiles(const char* rawId, LuaAppManifest& manifest, String& error) {
    String id = rawId ? rawId : "";
    if (!LuaAppManifestParser::validId(id)) {
        error = "invalid_id";
        return false;
    }

    auto& storage = StorageService::instance();
    auto* fs = stageFs(id.c_str());
    if (!fs) {
        error = "appfs_unavailable";
        return false;
    }

    const String stage = stageDir(id);
    String manifestPath = stage + "/app.json";
    if(!validatePackageAssets(*fs,stage)){error="invalid_package_assets";return false;}
    if (!fs->exists(manifestPath)) {
        manifestPath = stage + "/manifest.json";
    }

    if (!LuaAppManifestParser::load(*fs, manifestPath, manifest, error, true)) {
        return false;
    }

    if (manifest.id != id) {
        error = "manifest_id_mismatch";
        return false;
    }

    if (!validateLuaSyntax(*fs, stage + "/main.lua", manifest, error)) {
        return false;
    }
    if (manifest.settings && !validateLuaSyntax(*fs,stage+"/settings.lua",manifest,error))return false;
    // Optional members must also be syntactically valid when supplied.
    if (manifest.schema == 3 && !manifest.settings && fs->exists(stage+"/settings.lua")) {
        error="settings_file_unexpected";return false;
    }
    if (!manifest.settings && fs->exists(stage+"/settings.lua") &&
        !validateLuaSyntax(*fs,stage+"/settings.lua",manifest,error))return false;
    if (!validateWebSettingsJson(*fs, stage + "/web_settings.json", error)) return false;

    return true;
}

bool LuaPackageInstaller::validateStage(const char* rawId, LuaInstallPlan& plan, String& error) {
    LuaAppManifest manifest;
    if (!validateStagedFiles(rawId, manifest, error)) return false;
    return resolvePlanOnUi(String(rawId ? rawId : ""), manifest, plan, error);
}

bool LuaPackageInstaller::resolvePlanOnUi(
    const String& id, const LuaAppManifest& manifest, LuaInstallPlan& plan, String& error
) {
    // commit() can pass plan.manifest as `manifest`. Copy it before resetting
    // the output plan; otherwise that self-alias clears the incoming version
    // and a normal update is incorrectly classified as a downgrade.
    const LuaAppManifest manifestCopy = manifest;
    plan = {};
    plan.manifest = manifestCopy;

    auto* current = AppManager::instance().find(id.c_str());
    if (!current) {
        plan.mode = LuaInstallMode::Install;
        return true;
    }

    if (current->kind() != AppKind::Lua || !LuaAppRegistry::instance().owns(id.c_str())) {
        error = "id_conflict_firmware_app";
        return false;
    }

    // The dynamic registry object can retain a version label from a prior
    // discovery pass. The live package manifest is the authoritative version
    // for an installed Lua app and prevents a valid update being misclassified
    // as a downgrade.
    plan.currentVersion = current->version();
    auto& storage = StorageService::instance();
    if (auto* fs = stageFs(id.c_str())) {
        LuaAppManifest persisted;
        String manifestError;
        if (LuaAppManifestParser::load(
                *fs, liveDir(id) + "/app.json", persisted, manifestError) &&
            persisted.id == id) {
            plan.currentVersion = persisted.version;
        }
    }
    if (!plan.currentVersion.length()) {
        plan.mode = LuaInstallMode::Update;
        return true;
    }

    const int cmp = LuaAppManifestParser::compareVersions(manifestCopy.version, plan.currentVersion);
    Serial.printf("[LUA] install plan id=%s incoming=%s installed=%s cmp=%d\n",
                  id.c_str(), manifestCopy.version.c_str(),
                  plan.currentVersion.c_str(), cmp);
    if (cmp > 0) plan.mode = LuaInstallMode::Update;
    else if (cmp == 0) plan.mode = LuaInstallMode::Reinstall;
    else plan.mode = LuaInstallMode::Downgrade;

    return true;
}

bool LuaPackageInstaller::rollback(
    const String& id,
    bool hadLive,
    bool hadRegistered,
    bool oldEnabled,
    String& error
) {
    auto& storage = StorageService::instance();
    auto* fs = stageFs(id.c_str());
    if (!fs) {
        error += ":rollback_appfs_unavailable";
        return false;
    }

    LuaAppRegistry::instance().unregisterApp(id.c_str());

    // Do not rely on directory rename/rmdir on AppFS. Restore only the package
    // files, because those are the only files a Lua package may contain.
    const String live = liveDir(id);
    const String backup = backupDir(id);
    clearPackageFiles(*fs, live);

    if (hadLive) {
        if (!fs->exists(backup) || !copyPackageFiles(*fs, backup, live)) {
            error += ":rollback_restore_failed";
            return false;
        }
    }

    // If the previous app came from SD, rediscovery restores it as well.
    LuaAppRegistry::instance().discoverAndRegister();

    if (hadRegistered) {
        auto* restored = AppManager::instance().find(id.c_str());
        if (!restored) {
            error += ":rollback_registry_failed";
            return false;
        }
        AppManager::instance().setEnabled(restored, oldEnabled);
    }

    return true;
}

bool LuaPackageInstaller::commit(
    const char* rawId,
    bool allowDowngrade,
    LuaInstallPlan& plan,
    String& error,
    bool filesValidated
) {
    String id = rawId ? rawId : "";

    // The HTTP worker already parsed/compiled the staged package. Its request
    // is serialized by httpd while the UI owns AppManager/LVGL state.
    if (filesValidated) {
        if (!LuaAppManifestParser::validId(id) || plan.manifest.id != id ||
            !resolvePlanOnUi(id, plan.manifest, plan, error)) return false;
    } else if (!validateStage(id.c_str(), plan, error)) {
        return false;
    }

    if (plan.mode == LuaInstallMode::Downgrade && !allowDowngrade) {
        error = "downgrade_confirmation_required:incoming=" +
                plan.manifest.version + ",installed=" + plan.currentVersion;
        return false;
    }

    auto& registry = LuaAppRegistry::instance();
    // Do not wait for the scan worker while holding the display mutex.
    if (!registry.beginPackageMutation(0)) {
        error = "package_scan_busy";
        return false;
    }
    struct MutationGuard {
        LuaAppRegistry& registry;
        ~MutationGuard() { registry.endPackageMutation(); }
    } transaction{registry};

    auto& storage = StorageService::instance();
    auto& manager = AppManager::instance();

    auto* oldApp = manager.find(id.c_str());
    Serial.printf("[LUA] install phase=activate id=%s heap=%u stack=%u\n",id.c_str(),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    if (oldApp && manager.current() == oldApp) {
        // An app cannot remain alive while its Lua state and package files are
        // replaced. Close the exact update target on the UI task, then restore
        // the launcher before the transactional file activation begins.
        manager.closeCurrent();
        Serial.printf("[LUA] install phase=target_closed id=%s\n",id.c_str());
        UiShell::instance().showHome();
    }
    const bool hadRegistered = oldApp != nullptr;
    const bool oldEnabled = oldApp ? manager.isEnabled(oldApp) : plan.manifest.enabled;
    auto* appFs = stageFs(id.c_str());
    if (!appFs) {
        error = "appfs_unavailable";
        return false;
    }

    const String live = liveDir(id);
    const String backup = backupDir(id);
    const String stage = stageDir(id);

    // A failed previous installation may have left a directory containing
    // only some package files. It must never be used as a rollback source.
    // Treat it as replaceable only when no registered app owns this ID.
    // Application data and secrets live outside the package folder and are
    // deliberately untouched. This also works when the app is stored on SD.
    const bool liveExists = appFs->exists(live);
    const bool liveHasManifest = appFs->exists(live + "/app.json") ||
                                 appFs->exists(live + "/manifest.json");
    const bool liveHasMain = appFs->exists(live + "/main.lua");
    const bool liveIncomplete = liveExists && (!liveHasManifest || !liveHasMain);
    if (liveIncomplete && hadRegistered) {
        error = "incomplete_registered_package:" + live;
        return false; // Never destroy the files of an active/registered app.
    }
    if (liveIncomplete) {
        // A valid saved rollback must be investigated instead of overwritten.
        const bool backupComplete = appFs->exists(backup) &&
            (appFs->exists(backup + "/app.json") ||
             appFs->exists(backup + "/manifest.json")) &&
            appFs->exists(backup + "/main.lua");
        if (backupComplete) {
            error = "incomplete_live_with_recovery_backup:" + live;
            return false;
        }
        EventLog::instance().add("WARN", "LUA_ORPHAN_REPAIR", id);
    }
    const bool hadLive = liveExists && !liveIncomplete;

    // AppFS/LittleFS on this target does not reliably rename non-empty
    // directories. Use file-level transactional activation instead.
    if (!ensureFsDir(*appFs, "/installer_backup")) {
        error = "backup_root_mkdir";
        return false;
    }

    // Remove only stale package files from an old empty backup directory.
    // The directory itself may be undeletable and is safe to reuse.
    if (appFs->exists(backup)) {
        clearPackageFiles(*appFs, backup);
    }

    String backupError;
    Serial.printf("[LUA] install phase=backup id=%s\n",id.c_str());
    if (hadLive && !copyPackageFiles(*appFs, live, backup,&backupError)) {
        // Live is still untouched. Do not leave an incomplete backup that
        // resetStage() mistakes for a valid recovery package on the next try.
        clearPackageFiles(*appFs, backup);
        appFs->rmdir(backup);
        error = "backup_current_failed:"+backupError;
        return false;
    }

    if (hadRegistered) {
        if (!LuaAppRegistry::instance().unregisterApp(id.c_str())) {
            clearPackageFiles(*appFs, backup);
            error = "unregister_current_failed";
            return false;
        }
    }

    if ((targetSd_||targetUsb_) && (!ensureFsDir(*appFs, "/Apps") || !ensureFsDir(*appFs, "/Apps/32"))) {
        error = "sd_apps_root_mkdir_failed"; return false;
    }
    if (!ensureFsDir(*appFs, live)) {
        error = "activate_live_mkdir_failed:" + id;
        rollback(id, hadLive, hadRegistered, oldEnabled, error);
        return false;
    }

    // Keep the live directory itself; replace only validated package files.
    clearPackageFiles(*appFs, live);
    if (!copyPackageFiles(*appFs, stage, live)) {
        error = "activate_stage_files_failed:" + id;
        rollback(id, hadLive, hadRegistered, oldEnabled, error);
        return false;
    }

    // Always clear any old/ghost Lua registration for this ID before the
    // freshly activated package is registered. This handles both normal
    // registry-owned objects and AppManager-only ghosts left by earlier failed
    // install attempts.
    if (!registry.forceRemoveRegistration(id.c_str())) {
        error = "registration_cleanup_failed:" + id;
        rollback(id, hadLive, hadRegistered, oldEnabled, error);
        return false;
    }

    bool registered = registry.registerValidatedApp(plan.manifest, live, !targetSd_&&!targetUsb_,targetUsb_,targetUsbGeneration_);
    Serial.printf("[LUA] install phase=registered id=%s ok=%u\n",id.c_str(),unsigned(registered));
    auto* installed = manager.find(id.c_str());

    // If the first attempt still collided, capture the conflicting object,
    // remove a Lua ghost once more and retry direct registration exactly once.
    if (!registered) {
        auto* conflict = manager.find(id.c_str());
        if (conflict && conflict->kind() == AppKind::Lua &&
            manager.current() != conflict) {
            registry.forceRemoveRegistration(id.c_str());
            registered = registry.registerValidatedApp(plan.manifest, live, !targetSd_&&!targetUsb_,targetUsb_,targetUsbGeneration_);
            installed = manager.find(id.c_str());
        }
    }

    // Last recovery path: filesystem discovery of the now-live package.
    if (!installed) {
        registry.discoverAndRegister();
        installed = manager.find(id.c_str());
    }

    // A scan queued before this transaction can have completed registration
    // just before the direct attempt. That is already a valid activation: do
    // not roll it back merely because registerValidatedApp() reported the
    // duplicate. The package files were validated before activation and copied
    // while packageIoMutex_ is held, so an existing registry object for this
    // Lua ID is sufficient. Its version label can lag until the next scan.
    const bool installedMatches =
        installed &&
        installed->kind() == AppKind::Lua &&
        registry.owns(id.c_str());
    if (!installedMatches) {
        String existingVersion = installed ? String(installed->version()) : "-";
        String existingKind = installed ? String((int)installed->kind()) : "-";
        error =
            "post_install_registration_failed:"
            "id=" + id +
            ",manager=" + String((unsigned long)manager.count()) +
            ",dynamic=" + String((unsigned long)registry.dynamicCount()) +
            ",owned=" + String(registry.owns(id.c_str()) ? 1 : 0) +
            ",direct=" + String(registered ? 1 : 0) +
            ",kind=" + existingKind +
            ",version=" + existingVersion;
        rollback(id, hadLive, hadRegistered, oldEnabled, error);
        return false;
    }

    if (!registered) {
        EventLog::instance().add("INFO", "LUA_INSTALL_REUSED", id);
    }
    static_cast<LuaApp*>(installed)->configureWebSettings(appFs->exists((live+"/web_settings.json").c_str()));

    // Failure to persist the enabled state must trigger rollback too.
    String enabledError;
    if (!manager.setEnabled(installed, hadRegistered ? oldEnabled : plan.manifest.enabled,&enabledError)) {
        error = "enabled_state_persist_failed:"+enabledError;
        rollback(id, hadLive, hadRegistered, oldEnabled, error);
        return false;
    }

    // Commit complete. Package files in the backup are no longer needed.
    // Best-effort rmdir only; an empty leftover directory is harmless.
    if (hadLive) {
        clearPackageFiles(*appFs, backup);
        appFs->rmdir(backup);
    }

    // The stage directory is intentionally kept reusable. Remove its files so
    // the next upload starts clean even if LittleFS refuses rmdir.
    clearPackageFiles(*appFs, stage);
    appFs->rmdir(stage);

    EventLog::instance().add(
        "INFO",
        "LUA_INSTALL_COMMIT",
        id + " " + modeName(plan.mode) + " " +
        plan.currentVersion + " -> " + plan.manifest.version
    );

    return true;
}

bool LuaPackageInstaller::commitFromHttp(
    const char* rawId, bool allowDowngrade, LuaInstallPlan& plan, String& error
) {
    const String id = rawId ? rawId : "";
    LuaAppManifest manifest;
    // Expensive fs::File reads, manifest validation and Lua bytecode compilation
    // happen in the HTTP worker, not under the display/LVGL mutex.
    if (!validateStagedFiles(id.c_str(), manifest, error)) return false;

    PendingCommit request;
    request.id = id;
    request.allowDowngrade = allowDowngrade;
    request.plan.manifest = manifest;
    request.finished = xSemaphoreCreateBinary();
    if (!request.finished) { error = "installer_queue_memory"; return false; }

    bool accepted = false;
    portENTER_CRITICAL(&pendingCommitMux);
    if (!pendingCommit) { pendingCommit = &request; accepted = true; }
    portEXIT_CRITICAL(&pendingCommitMux);
    if (!accepted) {
        vSemaphoreDelete(request.finished);
        error = "installer_busy";
        return false;
    }

    // UI owns all registry changes and performs the short atomic activation.
    // The request stays alive until the UI acknowledges completion; do not
    // release stack data while the UI could still be using it.
    if (xSemaphoreTake(request.finished, pdMS_TO_TICKS(60000)) != pdTRUE) {
        bool cancelled = false;
        portENTER_CRITICAL(&pendingCommitMux);
        if (pendingCommit == &request && !request.processing) {
            pendingCommit = nullptr;
            cancelled = true;
        }
        portEXIT_CRITICAL(&pendingCommitMux);
        if (cancelled) {
            vSemaphoreDelete(request.finished);
            error = "installer_ui_timeout";
            return false;
        }
        // Already running on the UI: must await its result to preserve the
        // previous package/rollback and prevent a dangling request pointer.
        xSemaphoreTake(request.finished, portMAX_DELAY);
    }

    plan = request.plan;
    error = request.error;
    const bool success = request.success;
    vSemaphoreDelete(request.finished);
    return success;
}

bool LuaPackageInstaller::uiCommitPending() const {
    bool queued;
    portENTER_CRITICAL(&pendingCommitMux);
    queued = pendingCommit && !pendingCommit->processing;
    portEXIT_CRITICAL(&pendingCommitMux);
    return queued;
}

void LuaPackageInstaller::processPendingUiCommit() {
    // A filesystem scan already owns packageIoMutex. Never block the UI
    // waiting for the low-priority worker: revisit this request next frame.
    if (LuaAppRegistry::instance().scanBusy()) return;
    PendingCommit* request = nullptr;
    portENTER_CRITICAL(&pendingCommitMux);
    if (pendingCommit && !pendingCommit->processing) {
        pendingCommit->processing = true;
        request = pendingCommit;
    }
    portEXIT_CRITICAL(&pendingCommitMux);
    if (!request) return;

    // Called ONLY from UiShell::loop() with DisplayUiGuard held. No worker or
    // httpd callback may remove an active Lua app or mutate LVGL/AppManager.
    request->success = commit(request->id.c_str(), request->allowDowngrade,
                              request->plan, request->error, true);
    if (request->success) {
        AppRegistry::instance().rebuild();
        UiShell::instance().refreshLauncherAfterInstall();
    }
    portENTER_CRITICAL(&pendingCommitMux);
    pendingCommit = nullptr;
    portEXIT_CRITICAL(&pendingCommitMux);
    xSemaphoreGive(request->finished);
}

bool LuaPackageInstaller::cancel(const char* rawId) {
    String id = rawId ? rawId : "";
    if (!LuaAppManifestParser::validId(id)) return false;
    auto* fs = stageFs(id.c_str());
    if (!fs) return false;
    clearPackageFiles(*fs, stageDir(id));
    fs->rmdir(stageDir(id));
    return true;
}

fs::FS* LuaPackageInstaller::stageFs(const char* id) const {
    if (!id || targetId_ != id) return nullptr;
    if(targetUsb_)return UsbStorageService::instance().validFiles(targetUsbGeneration_)&&!UsbStorageService::instance().formatBusy()?targetUsbFs_.get():nullptr;
    return StorageService::instance().fsFor(targetSd_ ? StorageClass::Media : StorageClass::AppPackage);
}

}
