#include "PowerManager.h"
#include "SettingsService.h"
#include "IdleService.h"
#include "../core/EventBus.h"
#include "HomePanelConfig.h"
#include <esp_err.h>

namespace hp {

PowerManager& PowerManager::instance() {
    static PowerManager p;
    return p;
}

bool PowerManager::begin() {
#if !HOMEPANEL_FEATURE_POWER_MANAGEMENT
    return false;
#else
    settingsChanged();
    requestBurst(2500);
    initialized_ = apply(true);
    return initialized_;
#endif
}

void PowerManager::settingsChanged() {
    const auto s = SettingsService::instance().snapshot();
    energySaving_ = s.powerManagement.energySaving;
    requestedLightSleep_ = s.powerManagement.autoLightSleepWhenOff;
    apply(false);
}

void PowerManager::requestBurst(uint32_t durationMs) {
#if HOMEPANEL_FEATURE_POWER_MANAGEMENT
    const uint32_t until = millis() + max<uint32_t>(durationMs, 100);
    portENTER_CRITICAL(&mux_);
    if ((int32_t)(until - burstUntil_) > 0) burstUntil_ = until;
    portEXIT_CRITICAL(&mux_);
    apply(false);
#else
    (void)durationMs;
#endif
}

bool PowerManager::burstActive() const {
    uint32_t until;
    portENTER_CRITICAL(&mux_);
    until = burstUntil_;
    portEXIT_CRITICAL(&mux_);
    return until && (int32_t)(until - millis()) > 0;
}

PowerLevel PowerManager::wantedLevel() const {
    if (!energySaving_) return PowerLevel::FullSpeed;

    uint16_t users;
    portENTER_CRITICAL(&mux_);
    users = performanceUsers_;
    portEXIT_CRITICAL(&mux_);

    if (users > 0) return PowerLevel::Performance;
    if (burstActive()) return PowerLevel::AdaptiveBurst;

    switch (IdleService::instance().stage()) {
        case IdleStage::Active:
        case IdleStage::Dimmed:
            return PowerLevel::EcoCruise;
        case IdleStage::Saver:
        case IdleStage::Off:
            return PowerLevel::DeepEco;
    }
    return PowerLevel::EcoCruise;
}

bool PowerManager::apply(bool force) {
#if !HOMEPANEL_FEATURE_POWER_MANAGEMENT
    return false;
#else
    const PowerLevel wanted = wantedLevel();

    int minFreq = minMHz();
    int maxFreq = cruiseMHz();

    switch (wanted) {
        case PowerLevel::FullSpeed:
            minFreq = siliconMaxMHz();
            maxFreq = siliconMaxMHz();
            break;
        case PowerLevel::AdaptiveBurst:
            // A short burst should not immediately drop to the 40 MHz floor
            // in the middle of touch gestures or LVGL layout work.
            minFreq = siliconMaxMHz();
            maxFreq = siliconMaxMHz();
            break;
        case PowerLevel::EcoCruise:
            minFreq = minMHz();
            maxFreq = cruiseMHz();
            break;
        case PowerLevel::DeepEco:
            minFreq = minMHz();
            maxFreq = minMHz();
            break;
        case PowerLevel::Performance:
            minFreq = minMHz();
            maxFreq = siliconMaxMHz();
            break;
    }

    bool enableLightSleep = false;
#if HOMEPANEL_POWER_ALLOW_LIGHT_SLEEP
    enableLightSleep =
        energySaving_ &&
        requestedLightSleep_ &&
        IdleService::instance().stage() == IdleStage::Off;
#endif

    // Reconfigure only when actual limits change; avoid clock-tree churn.
    if (initialized_ &&
        minFreq == appliedMin_ &&
        maxFreq == appliedMax_ &&
        enableLightSleep == appliedLightSleep_ &&
        wanted == level_) {
        return true;
    }

    esp_pm_config_t cfg{};
    cfg.max_freq_mhz = maxFreq;
    cfg.min_freq_mhz = minFreq;
    cfg.light_sleep_enable = enableLightSleep;

    const esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[POWER] esp_pm_configure failed: %s\n", esp_err_to_name(err));
        return false;
    }

    const PowerLevel old = level_;
    appliedMin_ = minFreq;
    appliedMax_ = maxFreq;
    appliedLightSleep_ = enableLightSleep;
    level_ = wanted;
    initialized_ = true;
    publishIfChanged(old);
    return true;
#endif
}

bool PowerManager::acquirePerformance() {
#if !HOMEPANEL_FEATURE_POWER_MANAGEMENT
    return false;
#else
    bool first = false;

    portENTER_CRITICAL(&mux_);
    if (performanceUsers_ == UINT16_MAX) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    first = (performanceUsers_ == 0);
    performanceUsers_++;
    portEXIT_CRITICAL(&mux_);

    if (first) {
        if (!apply(true)) {
            portENTER_CRITICAL(&mux_);
            performanceUsers_--;
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        if (!cpuMaxLock_ &&
            esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "hp-perf", &cpuMaxLock_) != ESP_OK) {
            portENTER_CRITICAL(&mux_);
            performanceUsers_--;
            portEXIT_CRITICAL(&mux_);
            apply(true);
            return false;
        }
    }

    if (!cpuMaxLock_ || esp_pm_lock_acquire(cpuMaxLock_) != ESP_OK) {
        portENTER_CRITICAL(&mux_);
        if (performanceUsers_ > 0) performanceUsers_--;
        const bool none = performanceUsers_ == 0;
        portEXIT_CRITICAL(&mux_);
        if (none) apply(true);
        return false;
    }
    return true;
#endif
}

void PowerManager::releasePerformance() {
#if HOMEPANEL_FEATURE_POWER_MANAGEMENT
    bool releaseLock = false;
    bool last = false;

    portENTER_CRITICAL(&mux_);
    if (performanceUsers_ > 0) {
        performanceUsers_--;
        releaseLock = true;
        last = performanceUsers_ == 0;
    }
    portEXIT_CRITICAL(&mux_);

    if (releaseLock && cpuMaxLock_) esp_pm_lock_release(cpuMaxLock_);

    if (last) {
        requestBurst(350);
        apply(true);
    }
#endif
}

void PowerManager::loop() {
#if HOMEPANEL_FEATURE_POWER_MANAGEMENT
    const uint32_t now = millis();
    // Power policy can change only at a burst deadline or an idle-stage
    // boundary.  Checking it on every 5 ms main-loop pass just repeats mutex
    // and state comparisons; requestBurst/settingsChanged still apply at once.
    if (lastPolicyCheckMs_ && (uint32_t)(now - lastPolicyCheckMs_) < 50UL) return;
    lastPolicyCheckMs_ = now;
    apply(false);
#endif
}

PowerStatus PowerManager::status() const {
    PowerStatus s;
    s.initialized = initialized_;
    s.energySaving = energySaving_;
    s.lightSleepEnabled = appliedLightSleep_;
    s.level = level_;
    s.minFreqMHz = appliedMin_;
    s.maxFreqMHz = appliedMax_;
    s.siliconMaxMHz = siliconMaxMHz();
    s.cruiseMHz = cruiseMHz();

    portENTER_CRITICAL(&mux_);
    s.performanceUsers = performanceUsers_;
    const uint32_t until = burstUntil_;
    portEXIT_CRITICAL(&mux_);

    const int32_t remain = (int32_t)(until - millis());
    s.burstRemainingMs = remain > 0 ? (uint32_t)remain : 0;
    return s;
}

void PowerManager::publishIfChanged(PowerLevel oldLevel) {
    if (oldLevel != level_) {
        EventBus::instance().publish({EventType::PowerStateChanged, nullptr});
    }
}

}
