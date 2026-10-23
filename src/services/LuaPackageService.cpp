#include "LuaPackageService.h"
#include "LuaAppDataCleanupService.h"
#include "StorageService.h"
#include "UsbAppFileSystem.h"
#include "UsbStorageService.h"
#include "ConfigService.h"
#include "../apps/lua/LuaAppRegistry.h"
#include "../core/AppManager.h"
#include "../core/EventLog.h"
#include "../ui/UiShell.h"
#include <FS.h>

namespace hp {
namespace {

bool validId(const String& id) {
    if (!id.length() || id.length() > 31) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

bool eraseTree(fs::FS& fs, const String& path) {
    File node = fs.open(path, FILE_READ);
    if (!node) return true;
    if (!node.isDirectory()) { node.close(); return fs.remove(path); }
    File child = node.openNextFile();
    while (child) {
        const String childPath = child.path();
        const bool dir = child.isDirectory();
        child.close();
        if (!(dir ? eraseTree(fs, childPath) : fs.remove(childPath))) {
            node.close(); return false;
        }
        child = node.openNextFile();
    }
    node.close();
    return fs.rmdir(path);
}

bool packageExists(const String& id) {
    auto& storage = StorageService::instance();
    if(auto fs=usbAppFileSystemFor(UsbStorageService::instance().generation())){
        for(const auto& root:{"/Apps/32/","/apps/"})if(fs->exists(String(root)+id+"/main.lua"))return true;
    }
    if (storage.appFsAvailable()) {
        if (auto* fs = storage.fsFor(StorageClass::AppPackage)) {
            const String paths[] = {
                "/" + id + "/main.lua", "/apps/" + id + "/main.lua",
                "/Apps/" + id + "/main.lua", "/Apps/32/" + id + "/main.lua"
            };
            for (const auto& path : paths) if (fs->exists(path)) return true;
        }
    }
    if (storage.sdAvailable()) {
        const String paths[] = {
            "/Apps/32/" + id + "/main.lua", "/apps/32/" + id + "/main.lua",
            "/apps/" + id + "/main.lua"
        };
        for (const auto& path : paths) if (storage.sdAbsoluteExists(path)) return true;
    }
    return false;
}

} // namespace

LuaPackageService& LuaPackageService::instance() {
    static LuaPackageService service;
    return service;
}

LuaPackageRemoveResult LuaPackageService::remove(const String& id, bool purgeData) {
    LuaPackageRemoveResult result;
    if (!validId(id)) { result.error = "Ungültige App-ID"; return result; }

    auto* app = AppManager::instance().find(id.c_str());
    if (!app || app->kind() != AppKind::Lua ||
        !LuaAppRegistry::instance().owns(id.c_str()) || !packageExists(id)) {
        result.error = "Kein löschbares Lua-App-Paket gefunden";
        return result;
    }
    if (AppManager::instance().current() == app) {
        result.error = "Die App ist geöffnet. Bitte zuerst Home öffnen.";
        return result;
    }

    if (purgeData) {
        String preflightError;
        if (!LuaAppDataCleanupService::instance().preflight(id, preflightError)) {
            result.error = "Komplett-Löschen abgebrochen: App-Daten konnten nicht sicher geprüft werden: " + preflightError;
            return result;
        }
    }

    auto& registry = LuaAppRegistry::instance();
    if (!registry.beginPackageMutation()) {
        result.error = "App-Scan ist noch aktiv. Bitte erneut versuchen.";
        return result;
    }
    struct Guard { LuaAppRegistry& registry; ~Guard(){ registry.endPackageMutation(); } } guard{registry};

    // Complete deletion is two-phase: validate first, purge app-owned data
    // while the package is still intact, then remove the package only after
    // the data phase succeeded. This prevents the old failure mode where the
    // program vanished first and a later NVS/file failure left an orphaned
    // half-deleted app.
    if (purgeData) {
        const auto cleanup = LuaAppDataCleanupService::instance().erase(id);
        if (!cleanup.ok) {
            result.error =
                "Komplett-Löschen abgebrochen; App-Paket bleibt installiert. "
                "Bereits entfernt: KV=" + String((unsigned long)cleanup.kvRemoved) +
                ", Secrets=" + String((unsigned long)cleanup.secretsRemoved) +
                ", Dateien=" + String(cleanup.filesRemoved ? 1 : 0) +
                ". Fehler: " + cleanup.error;
            return result;
        }
        if (!ConfigService::instance().removeAppEnabled(id.c_str())) {
            result.error = "App-Daten wurden gelöscht, Aktivierungsstatus konnte aber nicht entfernt werden";
            return result;
        }
        result.dataPurged = true;
        EventLog::instance().add("INFO", "LUA_APP_PURGE",
            id + " kv=" + String((unsigned long)cleanup.kvRemoved) +
            " secrets=" + String((unsigned long)cleanup.secretsRemoved) +
            " files=" + String(cleanup.filesRemoved ? 1 : 0));
    }

    bool removed = false;
    const auto usbGeneration=UsbStorageService::instance().generation();
    if(auto fs=usbAppFileSystemFor(usbGeneration)){
        for(const auto& root:{"/Apps/32/","/apps/"}){
            const String folder=String(root)+id;if(fs->exists(folder))removed|=eraseTree(*fs,folder);
        }
    }
    auto& storage = StorageService::instance();
    if (storage.appFsAvailable()) {
        if (auto* fs = storage.fsFor(StorageClass::AppPackage)) {
            const String folders[] = {"/" + id, "/apps/" + id, "/Apps/" + id, "/Apps/32/" + id};
            for (const auto& folder : folders)
                if (fs->exists(folder)) removed |= eraseTree(*fs, folder);
        }
    }
    if (storage.sdAvailable()) {
        if (auto* fs = storage.fsFor(StorageClass::Media)) {
            const String folders[] = {"/Apps/32/" + id, "/apps/32/" + id, "/apps/" + id};
            for (const auto& folder : folders)
                if (storage.sdAbsoluteExists(folder)) removed |= eraseTree(*fs, folder);
        }
    }

    if (!removed || packageExists(id)) {
        result.error = purgeData
            ? "App-Daten wurden gelöscht, aber das App-Paket konnte nicht vollständig entfernt werden"
            : "App-Paket konnte nicht vollständig gelöscht werden";
        return result;
    }
    result.packageRemoved = true;

    if (!registry.queueUnregister(id.c_str())) {
        result.error = "App wurde gelöscht; UI-Bereinigung erfolgt spätestens beim Neustart";
        return result;
    }
    UiShell::instance().requestAppsRefresh();
    EventLog::instance().add("INFO",
        purgeData ? "LUA_APP_REMOVE_ALL" : "LUA_APP_REMOVE_KEEP_DATA", id);
    result.ok = true;
    return result;
}

} // namespace hp
