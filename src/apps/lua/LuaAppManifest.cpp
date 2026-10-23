#include "LuaAppManifest.h"
#include "BuildInfo.h"
#include <ArduinoJson.h>
#include <climits>

namespace hp {
namespace {

int nextNumber(const String& value, size_t& pos) {
    while (pos < value.length() && !isDigit(value[pos])) ++pos;
    int n = 0;
    bool any = false;
    while (pos < value.length() && isDigit(value[pos])) {
        any = true;
        const int digit=value[pos]-'0';
        n = n > (INT_MAX-digit)/10 ? INT_MAX : n*10+digit;
        ++pos;
    }
    return any ? n : 0;
}

bool allowedVersionChar(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '+';
}

}

bool LuaAppManifestParser::validId(const String& id) {
    if (!id.length() || id.length() > 31) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!(islower((unsigned char)c) || isDigit(c) || c == '_' || c == '-')) return false;
    }
    return true;
}

bool LuaAppManifestParser::validVersion(const String& version) {
    if (!version.length() || version.length() > 24) return false;
    bool digit = false;
    for (size_t i = 0; i < version.length(); ++i) {
        if (!allowedVersionChar(version[i])) return false;
        if (isDigit(version[i])) digit = true;
    }
    return digit;
}

int LuaAppManifestParser::compareVersions(const String& a, const String& b) {
    // Numeric dotted prefix comparison. Suffixes such as 0.3.3b are tolerated.
    size_t pa = 0, pb = 0;
    for (int part = 0; part < 4; ++part) {
        const int na = nextNumber(a, pa);
        const int nb = nextNumber(b, pb);
        if (na < nb) return -1;
        if (na > nb) return 1;

        while (pa < a.length() && a[pa] != '.') ++pa;
        while (pb < b.length() && b[pb] != '.') ++pb;
        if (pa < a.length()) ++pa;
        if (pb < b.length()) ++pb;
    }
    return 0;
}

bool LuaAppManifestParser::firmwareCompatible(const String& minimum, String& error) {
    if (!minimum.length()) return true;
    if (!validVersion(minimum)) {
        error = "minFirmware_invalid";
        return false;
    }
    if (compareVersions(String(build::kVersion), minimum) < 0) {
        error = "firmware_too_old:" + String(build::kVersion) + "<" + minimum;
        return false;
    }
    return true;
}

bool LuaAppManifestParser::parseJson(const String& json, LuaAppManifest& out, String& error, bool newPackage) {
    if(!json.length()||json.length()>8192){error="manifest_size";return false;}
    JsonDocument doc;
    const auto parseError = deserializeJson(doc, json, DeserializationOption::NestingLimit(4));
    if (parseError) {
        error = "manifest_json";
        return false;
    }

    if (json.length() > 8192 || !doc["schema"].is<uint16_t>() ||
        doc["schema"].as<uint16_t>() != LuaAppManifest::kSchema) {
        error = "manifest_schema_v3_required";
        return false;
    }

    if (!doc["id"].is<const char*>() ||
        !doc["name"].is<const char*>() ||
        !doc["version"].is<const char*>() ||
        !doc["type"].is<const char*>() ||
        !doc["entry"].is<const char*>()) {
        error = "manifest_required_fields";
        return false;
    }

    if ((newPackage || !doc["settings"].isNull()) && !doc["settings"].is<bool>()) {
        error = "manifest_settings_boolean"; return false;
    }
    const bool entryProvided=!doc["settingsEntry"].isUnbound();
    if (entryProvided &&
        (!doc["settingsEntry"].is<const char*>() || doc["settingsEntry"].as<String>() != "settings.lua")) {
        error = "manifest_settings_entry"; return false;
    }
    if (newPackage && doc["settings"].as<bool>() && doc["settingsEntry"].isNull()) {
        error = "manifest_settings_entry"; return false;
    }
    out = {};
    error="";
    if(!doc["enabled"].isUnbound()&&!doc["enabled"].is<bool>()){error="manifest_enabled_boolean";return false;}
    out.schema = doc["schema"].as<uint16_t>();
    out.id = doc["id"].as<String>();
    out.name = doc["name"].as<String>();
    out.version = doc["version"].as<String>();
    out.type = doc["type"].as<String>();
    out.entry = doc["entry"].as<String>();
    out.icon = doc["icon"] | "home";
    out.minFirmware = doc["minFirmware"] | "";
    out.settings = doc["settings"] | false;
    out.settingsDeclared = doc["settings"].is<bool>();
    out.settingsEntryExplicit = entryProvided;
    out.enabled = doc["enabled"] | true;
    if(!doc["background"].isUnbound()){
        if(out.schema!=3||!doc["background"].is<JsonObject>()){error="manifest_background";return false;}
        JsonObject bg=doc["background"].as<JsonObject>();
        for(JsonPair field:bg){const String key=field.key().c_str();if(key!="enabled"&&key!="priority"&&key!="preferredIntervalMinutes"&&key!="maximumDelayMinutes"&&key!="requiresNetwork"&&key!="allowDuringHighLoad"){error="manifest_background_unknown";return false;}}
        if(!bg["enabled"].is<bool>()){error="manifest_background_enabled";return false;}
        AppBackgroundPolicy p;const String priority=bg["priority"]|"none";
        if(priority=="low")p.priority=AppBackgroundPriority::Low;else if(priority=="normal")p.priority=AppBackgroundPriority::Normal;else if(priority=="important")p.priority=AppBackgroundPriority::Important;else if(priority!="none"){error="manifest_background_priority";return false;}
        for(const char* key:{"preferredIntervalMinutes","maximumDelayMinutes"})if(!bg[key].isUnbound()&&(!bg[key].is<uint32_t>()||bg[key].as<uint32_t>()>10080)){error="manifest_background_interval";return false;}
        for(const char* key:{"requiresNetwork","allowDuringHighLoad"})if(!bg[key].isUnbound()&&!bg[key].is<bool>()){error="manifest_background_boolean";return false;}
        p.preferredIntervalMs=(bg["preferredIntervalMinutes"]|30u)*60000UL;p.maximumDelayMs=(bg["maximumDelayMinutes"]|60u)*60000UL;
        p.requiresNetwork=bg["requiresNetwork"]|false;p.allowDuringHighLoad=bg["allowDuringHighLoad"]|false;
        if(!validBackgroundPolicy(p)||(bg["enabled"].as<bool>()&&p.priority==AppBackgroundPriority::None)){error="manifest_background_interval";return false;}
        if(bg["enabled"].as<bool>())out.background=appPolicy(p); // Important needs native approval, never granted by a package.
    }
    if (out.schema == 3) {
        if (!out.settingsDeclared || (out.settings && !out.settingsEntryExplicit) ||
            (!out.settings && out.settingsEntryExplicit)) {
            error = "manifest_settings_entry"; return false;
        }
        if (!doc["permissions"].is<JsonArray>() || doc["permissions"].size() > 16) {
            error = "manifest_permissions"; return false;
        }
        for (JsonVariant value : doc["permissions"].as<JsonArray>()) {
            if (!value.is<const char*>()) { error = "manifest_permissions"; return false; }
            const String name = value.as<String>();
            AppPermissions bit = 0;
            if (name == "network.local") bit = perm(AppPermission::Network);
            else if (name == "network.internet") bit = perm(AppPermission::NetworkInternet);
            else if (name == "storage") bit = perm(AppPermission::UserStorage);
            else if (name == "secrets") bit = perm(AppPermission::Secrets);
            else if (name == "discovery") bit = perm(AppPermission::Discovery);
            else if (name == "mqtt") bit = perm(AppPermission::Mqtt);
            else if (name == "websocket") bit = perm(AppPermission::Websocket);
            else if (name == "home_assistant") bit = perm(AppPermission::HomeAssistant);
            else if (name == "energy") bit = perm(AppPermission::Energy);
            else if (name == "media") bit = perm(AppPermission::Audio);
            else if (name == "storage.read") bit = perm(AppPermission::StorageRead);
            else if (name == "storage.writeAppData") bit = perm(AppPermission::UserStorage);
            else if (name == "storage.nasRead") bit = perm(AppPermission::NasRead);
            else if (name == "system.wallpaper") bit = perm(AppPermission::Wallpaper);
            else if (name == "power.idleIntegration") bit = perm(AppPermission::IdleIntegration);
            else if (name == "calendar.read") bit = perm(AppPermission::CalendarRead);
            else if (name == "calendar.write") bit = perm(AppPermission::CalendarWrite);
            else if (name == "camera") bit = perm(AppPermission::Camera);
            else if (name == "microphone") bit = perm(AppPermission::Microphone);
            else if (name == "intercom") bit = perm(AppPermission::Intercom);
            else if (name == "door.open") bit = perm(AppPermission::DoorOpen);
            else { error = "manifest_permission_unknown"; return false; }
            if (out.permissions & bit) { error = "manifest_permission_duplicate"; return false; }
            out.permissions |= bit;
        }
    }

    out.id.trim();
    out.name.trim();
    out.version.trim();
    out.type.trim();
    out.entry.trim();
    out.icon.trim();
    out.minFirmware.trim();

    if (!validId(out.id)) {
        error = "manifest_id";
        return false;
    }
    if (!out.name.length() || out.name.length() > 48) {
        error = "manifest_name";
        return false;
    }
    if (!validVersion(out.version)) {
        error = "manifest_version";
        return false;
    }
    if (out.type != "lua") {
        error = "manifest_type";
        return false;
    }
    if (out.entry != "main.lua") {
        error = "manifest_entry";
        return false;
    }
    if (out.icon.length() > 31) {
        error = "manifest_icon";
        return false;
    }
    if (!firmwareCompatible(out.minFirmware, error)) {
        return false;
    }

    return true;
}

bool LuaAppManifestParser::load(fs::FS& fs, const String& path, LuaAppManifest& out, String& error, bool newPackage) {
    File f = fs.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        error = "manifest_missing";
        return false;
    }
    if (f.size() == 0 || f.size() > 8192) {
        f.close();
        error = "manifest_size";
        return false;
    }

    const size_t expected = f.size();
    String json = f.readString();
    f.close();
    if(json.length()!=expected||json.length()!=strlen(json.c_str())){
        error="manifest_read_failed";
        return false;
    }
    return parseJson(json, out, error, newPackage);
}

}
