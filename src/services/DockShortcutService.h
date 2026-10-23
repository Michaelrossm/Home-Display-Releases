#pragma once
#include <Arduino.h>
#include <cstddef>

namespace hp {
// Persistent dock shortcuts. Never installs, disables or deletes an app.
class DockShortcutService {
public:
    static constexpr size_t kMaxItems = 48; // Same bounded capacity as AppRegistry.
    static constexpr size_t kIdLength = 40;
    static DockShortcutService& instance();
    size_t load(char ids[][kIdLength], size_t capacity) const;
    bool add(const char* id);
    bool remove(const char* id);
private:
    static constexpr const char* kKey = "ui_dock_apps";
    static constexpr const char* kDefaults = "radio|music|weather|settings";
    static bool validId(const char* id);
    static bool hasId(const String& list, const char* id);
    static String without(const String& list, const char* id);
    static String savedList();
};
}
