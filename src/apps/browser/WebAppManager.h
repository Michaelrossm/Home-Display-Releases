#pragma once
#include <Arduino.h>
#include <FS.h>
#include "../../core/AppPermissions.h"

namespace hp {

enum class WebAppRenderMode : uint8_t { Auto = 0, MiniBrowser = 1, NativeAdapter = 2 };

#pragma pack(push, 1)
struct LegacyWebAppRecord {
    uint32_t id = 0;
    uint8_t enabled = 1;
    uint8_t renderMode = (uint8_t)WebAppRenderMode::Auto;
    uint16_t reserved = 0;
    char name[40]{};
    char url[160]{};
};

struct WebAppRecord {
    uint32_t id = 0;
    uint16_t schema = 2;
    uint16_t permissions = perm(AppPermission::Network) | perm(AppPermission::UserStorage);
    uint8_t enabled = 1;
    uint8_t renderMode = (uint8_t)WebAppRenderMode::Auto;
    uint8_t fullscreen = 0;
    uint8_t allowAudio = 0;
    uint8_t keepAwake = 0;
    uint8_t reserved[3]{};
    char name[40]{};
    char url[160]{};
    char icon[96]{};
};

struct WebAppFileHeader {
    uint32_t magic = 0x48445741;
    uint16_t schema = 2;
    uint16_t recordSize = sizeof(WebAppRecord);
    uint16_t count = 0;
    uint16_t reserved = 0;
};
#pragma pack(pop)

class WebAppManager {
public:
    static constexpr size_t kMaxApps = 16;
    static constexpr const char* kInstalledAppsPath = "/webapps/apps.bin";

    static WebAppManager& instance();

    bool begin();
    bool reload();
    size_t count() const { return count_; }
    const WebAppRecord* at(size_t i) const { return i < count_ ? &apps_[i] : nullptr; }

    bool add(const char* name, const char* url, WebAppRenderMode mode = WebAppRenderMode::Auto);
    bool remove(size_t index);
    bool setEnabled(size_t index, bool enabled);
    bool validateUrl(const char*, String* reason = nullptr) const;
    bool validateRecord(const WebAppRecord&, String* reason = nullptr) const;

    // Compatibility bridge for old callers. App enable/disable state is now
    // owned by AppManager, not by the Web-App database.
    bool nativeInstalled(const char* id) const;
    bool setNativeInstalled(const char* id, bool on);

private:
    bool load();
    bool loadLegacy(fs::FS&, const String&, size_t);
    bool save();
    bool importFromSd();
    uint32_t makeId(const char*, const char*) const;

    WebAppRecord apps_[kMaxApps]{};
    size_t count_ = 0;
};

}
