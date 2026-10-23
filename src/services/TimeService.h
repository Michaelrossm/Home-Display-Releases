#pragma once
#include <Arduino.h>
#include <time.h>

namespace hp {

enum class TimeSource : uint8_t {
    Invalid,
    Restored,
    Manual,
    Network
};

class TimeService {
public:
    static TimeService& instance();

    bool begin();
    void loop();

    bool valid() const;
    time_t nowUtc() const;

    // Cached, non-blocking UI strings. No getLocalTime() wait on every call.
    String timeText() const;
    String dateText() const;

    bool localNow(struct tm& out) const;
    bool localFromEpoch(time_t epoch, struct tm& out) const;
    String formatLocal(time_t epoch, const char* format,
                       const char* fallback = "") const;

    // Set the local German date/time without requiring Internet.
    // The value is persisted immediately and NTP may correct it later.
    bool setManualLocal(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute);

    TimeSource source() const { return source_; }
    bool restoredFromStorage() const { return restoredAtBoot_; }
    bool networkSynchronized() const { return source_ == TimeSource::Network; }
    const char* sourceName() const;

private:
    void refreshCache(time_t now);
    void persistIfNeeded(time_t now, bool force = false);

    static constexpr time_t kMinValidEpoch = 1700000000; // 2023-11-14
    // UI displays minutes, while UiShell refreshes its status once per second.
    // Formatting local time more often only consumes CPU in the main loop.
    static constexpr uint32_t kRefreshIntervalMs = 1000;
    static constexpr uint32_t kNtpSyncIntervalMs = 60UL * 60UL * 1000UL;
    static constexpr uint32_t kNormalPersistIntervalMs = 30UL * 60UL * 1000UL;
    static constexpr time_t kPersistDeltaSeconds = 300;

    char cachedTime_[6] = "--:--";
    char cachedDate_[11] = "--.--.----";

    uint32_t lastRefreshMs_ = 0;
    uint32_t lastPersistMs_ = 0;
    uint32_t bootMs_ = 0;

    time_t bootBaseEpoch_ = 0;
    time_t lastPersistedEpoch_ = 0;

    bool restoredAtBoot_ = false;
    bool awaitingNetworkCorrection_ = false;
    TimeSource source_ = TimeSource::Invalid;
};

}
