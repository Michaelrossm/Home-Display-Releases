#pragma once
#include <Arduino.h>
#include <cstdint>
#include <atomic>

namespace hp {

struct ThemePalette {
    uint32_t background;
    uint32_t backgroundAlt;
    uint32_t surface;
    uint32_t card;
    uint32_t border;
    uint32_t text;
    uint32_t textSecondary;
    uint32_t accent;
    uint32_t onAccent;
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    bool dark;
};

class ThemeCatalog {
public:
    static constexpr uint8_t kAccentCount = 6;
    static const char* accentName(uint8_t accent);
    static constexpr uint8_t kBuiltInCount = 8;
    static constexpr uint8_t kMaxSdThemes = 6;
    static constexpr uint8_t kMaxMode = kBuiltInCount + kMaxSdThemes - 1;

    static ThemeCatalog& instance();

    void begin();
    void refreshSd();

    uint8_t count() const { return static_cast<uint8_t>(kBuiltInCount + sdCount_); }
    bool validMode(uint8_t mode) const { return mode < count(); }
    uint8_t normalizeMode(uint8_t mode) const { return validMode(mode) ? mode : 0; }

    const char* name(uint8_t mode) const;
    const char* filename(uint8_t mode) const;
    uint8_t modeForFilename(const char* filename) const;
    const char* optionList() const { return optionList_; }
    ThemePalette palette(uint8_t mode, uint8_t accent) const;
    bool isDark(uint8_t mode) const;

public:
    struct BasePalette {
        uint32_t background;
        uint32_t backgroundAlt;
        uint32_t surface;
        uint32_t card;
        uint32_t border;
        uint32_t text;
        uint32_t textSecondary;
        uint32_t onAccent;
        uint32_t success;
        uint32_t warning;
        uint32_t error;
        bool dark;
    };

private:
    struct SdTheme {
        char filename[40]{};
        char name[24]{};
        BasePalette palette{};
    };

    bool loadThemeFile(const String& path, SdTheme& out);
    void scanSd(); // Boot before LVGL, or existing I/O worker only.
    static void scanWorker(void*);
    void rebuildOptions();

    bool ready_ = false;
    bool scanned_ = false;
    uint32_t lastScanMs_ = 0;
    uint8_t sdCount_ = 0;
    SdTheme sd_[kMaxSdThemes]{};
    char optionList_[320]{};
    SdTheme pendingSd_[kMaxSdThemes]{};
    uint8_t pendingCount_ = 0;
    std::atomic<bool> scanPending_{false}, resultReady_{false};
};

}
