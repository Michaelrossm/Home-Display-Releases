#pragma once
#include <Arduino.h>
#include <esp_pm.h>

namespace hp {

enum class PowerLevel : uint8_t {
    FullSpeed,
    AdaptiveBurst,
    EcoCruise,
    DeepEco,
    Performance
};

struct PowerStatus {
    bool initialized = false;
    bool energySaving = true;
    bool lightSleepEnabled = false;
    PowerLevel level = PowerLevel::EcoCruise;
    int minFreqMHz = 40;
    int maxFreqMHz = 0;
    int siliconMaxMHz = 0;
    int cruiseMHz = 0;
    uint16_t performanceUsers = 0;
    uint32_t burstRemainingMs = 0;
};

class PowerManager {
public:
    static PowerManager& instance();

    bool begin();
    void loop();
    PowerStatus status() const;

    void requestBurst(uint32_t durationMs = 1500);
    bool acquirePerformance();
    void releasePerformance();
    void settingsChanged();

    static constexpr int siliconMaxMHz() {
    #if defined(HOMEPANEL_P4_PROFILE_R3) && defined(CONFIG_ESP32P4_REV_MIN_300)
        return 400;
    #else
        return 360;
    #endif
    }

    static constexpr int cruiseMHz() {
    #if defined(HOMEPANEL_P4_PROFILE_R3)
        return 100;
    #else
        return 90;
    #endif
    }

    static constexpr int minMHz() { return 40; }

private:
    bool apply(bool force = false);
    PowerLevel wantedLevel() const;
    bool burstActive() const;
    void publishIfChanged(PowerLevel oldLevel);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t performanceUsers_ = 0;
    uint32_t burstUntil_ = 0;
    uint32_t lastPolicyCheckMs_ = 0;

    bool initialized_ = false;
    bool energySaving_ = true;
    bool requestedLightSleep_ = false;
    bool appliedLightSleep_ = false;
    PowerLevel level_ = PowerLevel::EcoCruise;
    int appliedMin_ = 0;
    int appliedMax_ = 0;
    esp_pm_lock_handle_t cpuMaxLock_ = nullptr;
};

class PerformanceGuard {
public:
    PerformanceGuard() : held_(PowerManager::instance().acquirePerformance()) {}
    ~PerformanceGuard() { if (held_) PowerManager::instance().releasePerformance(); }

    PerformanceGuard(const PerformanceGuard&) = delete;
    PerformanceGuard& operator=(const PerformanceGuard&) = delete;

private:
    bool held_ = false;
};

}
