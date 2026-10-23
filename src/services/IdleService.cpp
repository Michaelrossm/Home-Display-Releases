#include "IdleService.h"
#include "MediaSessionService.h"
#include "PowerManager.h"
#include "SecurityService.h"
#include "../core/EventBus.h"
#include <algorithm>

namespace hp {

IdleService& IdleService::instance() {
    static IdleService i;
    return i;
}

void IdleService::begin() {
    const uint32_t now = millis();
    lastActivity_.store(now, std::memory_order_release);
    activityGeneration_.store(1, std::memory_order_release);
    stage_.store(
        static_cast<uint8_t>(IdleStage::Active),
        std::memory_order_release);
}

void IdleService::activity() {
    idleActionActive_.store(false,std::memory_order_release);
    // Publish the timestamp first and generation second. loop() snapshots the
    // generation before evaluating the timer and verifies it again immediately
    // before a transition. Therefore a touch always wins over an older timer
    // decision, even when both tasks run at the same moment.
    lastActivity_.store(millis(), std::memory_order_release);
    activityGeneration_.fetch_add(1, std::memory_order_acq_rel);

    PowerManager::instance().requestBurst(1800);

    if (stage() != IdleStage::Active)
        change(IdleStage::Active);
}

void IdleService::setContext(IdleContext c) {
    const uint8_t next = static_cast<uint8_t>(c);
    const uint8_t old = ctx_.exchange(next, std::memory_order_acq_rel);
    if (old != next)
        activity();
}

void IdleService::keepAwake(IdleLease owner,uint32_t duration){const auto i=static_cast<size_t>(owner);if(i>=static_cast<size_t>(IdleLease::Count))return;
    leases_[i].store(duration?((millis()+(duration>5000?5000:duration))|1u):0,std::memory_order_release);
    activityGeneration_.fetch_add(1,std::memory_order_acq_rel);
}
bool IdleService::awakeLease(uint32_t now)const{for(const auto& lease:leases_){const auto end=lease.load(std::memory_order_acquire);if(end&&int32_t(end-now)>0)return true;}return false;}
IdleProfile IdleService::profile() const {
    const auto settings = SettingsService::instance().snapshot();
    const auto ctx = static_cast<IdleContext>(
        ctx_.load(std::memory_order_acquire));

    return ctx == IdleContext::Media
        ? settings.media
        : (ctx == IdleContext::Power ? settings.power : settings.normal);
}

void IdleService::change(IdleStage next) {
    const uint8_t nextRaw = static_cast<uint8_t>(next);
    const uint8_t oldRaw =
        stage_.exchange(nextRaw, std::memory_order_acq_rel);

    if (oldRaw == nextRaw)
        return;

    if (next == IdleStage::Off &&
        SecurityService::instance().lockAfterDisplayOff()) {
        SecurityService::instance().lockTablet();
    }

    // Do not publish a pointer to the atomic member itself. EventBus consumers
    // receive a stable enum value for the duration of this synchronous publish.
    IdleStage published = next;
    EventBus::instance().publish(
        {EventType::IdleStateChanged, &published});
}

void IdleService::loop() {
    static IdleProfile cached;
    static uint32_t lastProfile = 0;
    static uint32_t lastCheck = 0;

    const uint32_t now = millis();
    if (lastCheck &&
        static_cast<uint32_t>(now - lastCheck) < 250UL) {
        return;
    }
    lastCheck = now;

    if (!lastProfile ||
        static_cast<uint32_t>(now - lastProfile) >= 1000UL) {
        cached = profile();
        lastProfile = now;
    }

    // Snapshot the activity generation together with its timestamp.
    const uint32_t generation =
        activityGeneration_.load(std::memory_order_acquire);
    const uint32_t last =
        lastActivity_.load(std::memory_order_acquire);

    // Input can publish a timestamp newer than this loop's earlier `now`.
    // Unsigned subtraction would turn that into ~49 days of inactivity.
    // Read the clock AFTER the captured activity timestamp, not before it.
    // Unsigned elapsed arithmetic then also remains correct across millis wrap.
    const uint32_t sec = static_cast<uint32_t>(millis() - last) / 1000UL;

    IdleStage wanted = IdleStage::Active;
    if(idleActionActive()||(!SecurityService::instance().tabletLocked()&&awakeLease(millis()))){
        wanted=IdleStage::Active;
    } else if (cached.offAfter && sec >= cached.offAfter) {
        wanted = IdleStage::Off;
    } else if (cached.saverEnabled &&
               cached.saverAfter &&
               sec >= cached.saverAfter) {
        wanted = IdleStage::Saver;
    } else if (cached.dimAfter &&
               sec >= cached.dimAfter) {
        wanted = IdleStage::Dimmed;
    }

    // Critical race guard:
    // If a touch/activity happened after the snapshot, this timer decision is
    // obsolete and must never darken the display.
    if (activityGeneration_.load(std::memory_order_acquire) != generation)
        return;

    // A second timestamp check also protects against very long scheduling
    // stalls between the first snapshot and the transition.
    if (lastActivity_.load(std::memory_order_acquire) != last)
        return;

    if (wanted != stage())
        change(wanted);
}

} // namespace hp
