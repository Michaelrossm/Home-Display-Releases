#pragma once
#include <Arduino.h>
#include <FS.h>
#include "../../core/AppPermissions.h"
#include "../../core/AppLifecycleCore.h"

namespace hp {

struct LuaAppManifest {
    static constexpr uint16_t kSchema = 3;

    uint16_t schema = kSchema;
    String id;
    String name;
    String version;
    String type = "lua";
    String icon = "home";
    String entry = "main.lua";
    String settingsEntry = "settings.lua";
    String minFirmware;
    bool settings = false;
    bool settingsDeclared = false;
    bool settingsEntryExplicit = false;
    bool enabled = true;
    AppPermissions permissions = 0;
    AppBackgroundPolicy background;
};

class LuaAppManifestParser {
public:
    static bool parseJson(const String& json, LuaAppManifest& out, String& error, bool newPackage = false);
    static bool load(fs::FS& fs, const String& path, LuaAppManifest& out, String& error, bool newPackage = false);

    static bool validId(const String& id);
    static bool validVersion(const String& version);
    static int compareVersions(const String& a, const String& b);
    static bool firmwareCompatible(const String& minimum, String& error);
};

}
