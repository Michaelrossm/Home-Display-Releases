#pragma once
#include <Arduino.h>
#include <FS.h>
#include <memory>
#include "LuaAppManifest.h"

namespace hp {
class LuaScriptBuffer;

enum class LuaInstallMode : uint8_t {
    Install,
    Update,
    Reinstall,
    Downgrade
};

struct LuaInstallPlan {
    LuaAppManifest manifest;
    LuaInstallMode mode = LuaInstallMode::Install;
    String currentVersion;
};

class LuaPackageInstaller {
public:
    static LuaPackageInstaller& instance();

    bool resetStage(const char* id, String& error);
    bool stagePath(const char* id, const char* fileName, String& outPath, String& error) const;

    bool validateStage(const char* id, LuaInstallPlan& plan, String& error);
    // Called by the HTTP task: expensive file/syntax validation only, no AppManager/LVGL access.
    bool validateStagedFiles(const char* id, LuaAppManifest& manifest, String& error);
    // HTTP task waits for UI-owned activation. Never calls AppManager from HTTP.
    bool commitFromHttp(const char* id, bool allowDowngrade, LuaInstallPlan& plan, String& error);
    // Called once per UI-loop pass while the display lock is held.
    void processPendingUiCommit();
    bool uiCommitPending() const;
    bool commit(const char* id, bool allowDowngrade, LuaInstallPlan& plan, String& error,
                bool filesValidated = false);
    bool cancel(const char* id);
    fs::FS* stageFs(const char* id) const;

    static const char* modeName(LuaInstallMode mode);

private:
    bool validateLuaSyntax(fs::FS& fs, const String& path, const LuaAppManifest& manifest, String& error);
    bool callbackExists(const LuaScriptBuffer& script, const char* name) const;
    // Must only be invoked on the UI task.
    bool resolvePlanOnUi(const String& id, const LuaAppManifest& manifest,
                         LuaInstallPlan& plan, String& error);
    bool rollback(const String& id, bool hadLive, bool hadRegistered, bool oldEnabled, String& error);

    String stageDir(const String& id) const { return String("/installer_stage/") + id; }
    String liveDir(const String& id) const { return id == targetId_ ? targetLive_ : String("/") + id; }
    String backupDir(const String& id) const { return String("/installer_backup/") + id; }
    String targetId_, targetLive_;
    bool targetSd_ = false;
    bool targetUsb_ = false;
    uint32_t targetUsbGeneration_ = 0;
    std::unique_ptr<fs::FS> targetUsbFs_;
};

}
