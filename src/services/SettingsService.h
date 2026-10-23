#pragma once
#include <memory>
#include <Arduino.h>

namespace hp {

struct IdleProfile {
    uint8_t dimBrightness = 60;
    uint16_t dimAfter = 40;
    bool saverEnabled = true;
    uint8_t saverBrightness = 25;
    uint16_t saverAfter = 120;
    uint16_t offAfter = 600;
    bool alwaysOnEnabled = false;
    uint8_t alwaysOnBrightness = 20;
};

struct PowerSettings {
    bool energySaving = true;
    bool autoLightSleepWhenOff = false;
};

struct AmbientLightSettings {
    bool autoBrightness = false;
    uint8_t minBrightness = 25;
    uint8_t maxBrightness = 220;
    uint16_t darkLux = 5;
    uint16_t brightLux = 500;
};

struct BrowserSettings {
    bool allowHttp = true;
    bool allowHttps = true;
    bool allowWan = false;
    uint32_t maxPageBytes = 131072;
};

struct MusicSettings {
    uint8_t volume = 55;
    bool resumeLast = true;
    bool shuffle = false;
};

struct UiSettings {
    bool wallpaperEnabled = true;
    bool wakeOnSingleTap = false;
    uint8_t wallpaperDim = 36;
    String wallpaperMode = "cover";
    // 0..7 built-in themes, 8..13 optional SD themes.
    uint8_t themeMode = 0;
    uint8_t accent = 0; // 0 Blue, 1 Cyan, 2 Green, 3 Orange, 4 Red, 5 Purple
    String wallpaperPath = "/homepanel/media/wallpapers/home.jpg";
};

struct NetworkEcoSettings {
    bool wifiOffWhenLan = false;
    bool wifiPowerSave = true;
    bool aggressiveWifiSleep = false;
    bool adaptiveTxPower = true;
    uint16_t lanStableBeforeWifiOff = 5;

    bool wifiScheduleEnabled = false;
    uint16_t wifiScheduleStartMinute = 22 * 60;
    uint16_t wifiScheduleEndMinute = 6 * 60;
};

struct SettingsSnapshot {
    String hostname = "homepanel";
    String weatherRegion = "Muenchen";
    String powerHost;
    String powerPassword;
    bool energyDetailHistoryEnabled=false;
    uint8_t energyDetailHistoryIntervalMinutes=5;
    uint16_t energyDetailHistoryRetentionDays=0;
    bool energyDetailHistoryPreferProviderBackfill=true;

    uint8_t brightness = 210;
    uint8_t rotation = 0;

    IdleProfile normal;
    IdleProfile media{70, 90, true, 35, 300, 1200, false, 20};
    IdleProfile power{100, 120, true, 35, 600, 1800, false, 20};

    PowerSettings powerManagement;
    NetworkEcoSettings networkEco;
    UiSettings ui;
    AmbientLightSettings ambientLight;
    BrowserSettings browser;
    MusicSettings music;
};

class SettingsService {
public:
    static SettingsService& instance();
    bool begin();
    SettingsSnapshot snapshot();
    void save(const SettingsSnapshot& s);
    bool wakeOnSingleTap() const;
    bool setWakeOnSingleTap(bool enabled);
    bool setBrightness(uint8_t brightness);
    bool setWifiPowerSave(bool enabled);
    bool setWifiOffWhenLan(bool enabled);
    bool setWallpaperControls(bool enabled,uint8_t dim);
    bool setWallpaperMode(const String& mode);
    bool setTheme(uint8_t mode, uint8_t accent);

private:
    bool ok_ = false;
    struct CachedSettings {SettingsSnapshot value;uint32_t revision;};
    std::shared_ptr<const CachedSettings> cached_;
};

}
