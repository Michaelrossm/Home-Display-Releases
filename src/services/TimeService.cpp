#include "TimeService.h"
#include "ConfigService.h"
#include "../core/EventLog.h"

#include <esp_sntp.h>
#include <sys/time.h>
#include <stdlib.h>

namespace hp {

namespace {
constexpr const char* kPersistedEpochKey = "time_epoch";
constexpr const char* kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
}

TimeService& TimeService::instance() {
    static TimeService t;
    return t;
}

bool TimeService::begin() {
    bootMs_ = millis();

    // Restore the last known valid epoch first. This gives the UI an immediate
    // approximate clock even when the network is unavailable after reboot.
    auto& config = ConfigService::instance();
    const uint32_t saved = config.getU32(kPersistedEpochKey, 0);

    if (saved > static_cast<uint32_t>(kMinValidEpoch)) {
        timeval tv{};
        tv.tv_sec = static_cast<time_t>(saved);
        tv.tv_usec = 0;

        if (settimeofday(&tv, nullptr) == 0) {
            restoredAtBoot_ = true;
            bootBaseEpoch_ = static_cast<time_t>(saved);
            lastPersistedEpoch_ = static_cast<time_t>(saved);
            source_ = TimeSource::Restored;
            awaitingNetworkCorrection_ = true;
            refreshCache(bootBaseEpoch_);
            EventLog::instance().add(
                "INFO",
                "TIME_RESTORE",
                "Last known clock restored; NTP correction continues in background"
            );
        }
    }

    // Starts asynchronous SNTP. This call does not need to block the UI.
    configTzTime(
        kTimezone,
        "pool.ntp.org",
        "time.cloudflare.com"
    );
    // configTzTime starts SNTP asynchronously. The first query is therefore
    // immediate when there is no valid local time; later corrections are
    // deliberately limited to once per hour.
    esp_sntp_set_sync_interval(kNtpSyncIntervalMs);

    // If the system already had a valid clock from another source, cache it.
    const time_t now = time(nullptr);
    if (now > kMinValidEpoch) {
        if (!restoredAtBoot_) {
            bootBaseEpoch_ = now;
            source_ = TimeSource::Network;
        }
        refreshCache(now);
    }

    return true;
}

void TimeService::loop() {
    const uint32_t nowMs = millis();
    if (nowMs - lastRefreshMs_ < kRefreshIntervalMs) return;
    lastRefreshMs_ = nowMs;

    const time_t now = time(nullptr);
    if (now <= kMinValidEpoch) {
        source_ = TimeSource::Invalid;
        return;
    }

    refreshCache(now);

    // Detect a meaningful NTP correction relative to a restored or manually
    // entered clock. The UI never waits for this; it happens in the background.
    if (awaitingNetworkCorrection_) {
        const time_t expected =
            bootBaseEpoch_ +
            static_cast<time_t>((nowMs - bootMs_) / 1000UL);
        const time_t drift = llabs(
            static_cast<long long>(now) -
            static_cast<long long>(expected)
        );

        if (drift > 10) {
            awaitingNetworkCorrection_ = false;
            source_ = TimeSource::Network;
            persistIfNeeded(now, true);
            EventLog::instance().add(
                "INFO",
                "TIME_SYNC",
                "Network time corrected local clock"
            );
            return;
        }
    } else if (!restoredAtBoot_ && source_ == TimeSource::Invalid) {
        // First valid time after a cold boot without a saved timestamp.
        source_ = TimeSource::Network;
        persistIfNeeded(now, true);
        EventLog::instance().add(
            "INFO",
            "TIME_SYNC",
            "Network time acquired"
        );
        return;
    }

    persistIfNeeded(now, false);
}

bool TimeService::valid() const {
    return time(nullptr) > kMinValidEpoch;
}

time_t TimeService::nowUtc() const {
    return time(nullptr);
}

String TimeService::timeText() const {
    return String(cachedTime_);
}

String TimeService::dateText() const {
    return String(cachedDate_);
}

bool TimeService::localFromEpoch(time_t epoch, struct tm& out) const {
    if (epoch <= kMinValidEpoch) return false;
    localtime_r(&epoch, &out);
    return true;
}

bool TimeService::localNow(struct tm& out) const {
    return localFromEpoch(time(nullptr), out);
}

String TimeService::formatLocal(time_t epoch, const char* format,
                                const char* fallback) const {
    if (!format || !format[0]) return String(fallback ? fallback : "");
    struct tm local{};
    if (!localFromEpoch(epoch, local)) return String(fallback ? fallback : "");
    char buffer[48]{};
    if (!strftime(buffer, sizeof(buffer), format, &local))
        return String(fallback ? fallback : "");
    return String(buffer);
}

bool TimeService::setManualLocal(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t hour, uint8_t minute) {
    if (year < 2020 || year > 2099 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour > 23 || minute > 59) {
        return false;
    }

    // Interpret the entered values as local German time, including DST.
    setenv("TZ", kTimezone, 1);
    tzset();

    struct tm local{};
    local.tm_year = static_cast<int>(year) - 1900;
    local.tm_mon = static_cast<int>(month) - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;

    const time_t epoch = mktime(&local);
    if (epoch <= kMinValidEpoch) return false;

    // Reject normalized invalid dates such as 31.02.
    struct tm check{};
    localtime_r(&epoch, &check);
    if (check.tm_year != static_cast<int>(year) - 1900 ||
        check.tm_mon != static_cast<int>(month) - 1 ||
        check.tm_mday != day ||
        check.tm_hour != hour ||
        check.tm_min != minute) {
        return false;
    }

    timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return false;

    bootMs_ = millis();
    bootBaseEpoch_ = epoch;
    restoredAtBoot_ = true;
    awaitingNetworkCorrection_ = true;
    source_ = TimeSource::Manual;
    refreshCache(epoch);
    persistIfNeeded(epoch, true);

    EventLog::instance().add(
        "INFO",
        "TIME_MANUAL",
        "Clock set manually; NTP correction remains enabled"
    );
    return true;
}

const char* TimeService::sourceName() const {
    switch (source_) {
        case TimeSource::Network: return "network";
        case TimeSource::Manual: return "manual";
        case TimeSource::Restored: return "restored";
        default: return "invalid";
    }
}

void TimeService::refreshCache(time_t now) {
    struct tm local{};
    localtime_r(&now, &local);

    char newTime[sizeof(cachedTime_)]{};
    char newDate[sizeof(cachedDate_)]{};

    if (strftime(newTime, sizeof(newTime), "%H:%M", &local) == 0)
        return;
    if (strftime(newDate, sizeof(newDate), "%d.%m.%Y", &local) == 0)
        return;

    memcpy(cachedTime_, newTime, sizeof(cachedTime_));
    memcpy(cachedDate_, newDate, sizeof(cachedDate_));
}

void TimeService::persistIfNeeded(time_t now, bool force) {
    if (now <= kMinValidEpoch) return;
    if (now > static_cast<time_t>(UINT32_MAX)) return;

    const uint32_t nowMs = millis();
    const time_t delta = llabs(
        static_cast<long long>(now) -
        static_cast<long long>(lastPersistedEpoch_)
    );

    if (!force) {
        if (delta < kPersistDeltaSeconds) return;
        if (lastPersistMs_ &&
            nowMs - lastPersistMs_ < kNormalPersistIntervalMs)
            return;
    }

    if (ConfigService::instance().putU32(
            kPersistedEpochKey,
            static_cast<uint32_t>(now))) {
        lastPersistedEpoch_ = now;
        lastPersistMs_ = nowMs;
    }
}

}
