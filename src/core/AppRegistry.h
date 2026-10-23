#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "AppManager.h"

namespace hp {

enum class AppRegistryState : uint8_t { Enabled, Disabled, Error };

struct AppRegistryEntry {
    String id;
    String name;
    String version;
    String icon;
    String detail;
    String webPath;
    AppKind kind = AppKind::System;
    AppRegistryState state = AppRegistryState::Disabled;
    bool userInstallable = false;
    bool launcherVisible = false;
    bool removable = false;
    bool hasSettings = false;
    AppPermissions permissions = 0; // Safe worker snapshot, never dereference app there.
    IApp* app = nullptr;
    size_t backendIndex = SIZE_MAX;
};

class AppRegistry {
public:
    static AppRegistry& instance();

    // Rebuild is UI/installer owned. Web handlers consume snapshot()/lookup()
    // and never trigger a rebuild or filesystem discovery themselves.
    void rebuild();
    size_t count() const { return count_; }
    const AppRegistryEntry* at(size_t i) const { return i < count_ ? &entries_[i] : nullptr; }
    const AppRegistryEntry* find(const char* stableId) const;

    size_t snapshot(AppRegistryEntry* out, size_t capacity) const;
    bool copyAt(size_t index, AppRegistryEntry& out) const;
    bool lookup(const char* stableId, AppRegistryEntry& out) const;
    String userInstallableIdList() const;
    String launcherIdList() const;

    bool setEnabled(const char* stableId, bool enabled);
    bool removeWeb(const char* stableId);
    bool addWeb(const char* name, const char* url);

    static const char* kindName(AppKind kind);
    static const char* stateName(AppRegistryState state);

private:
    static constexpr size_t kMaxEntries = 48;
    static String webStableId(uint32_t id);
    SemaphoreHandle_t mutex() const;
    String idList(bool launcher) const;

    AppRegistryEntry entries_[kMaxEntries]{};
    size_t count_ = 0;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

}
