#include "MediaSessionService.h"
#include "SecurityService.h"
#include "../core/EventBus.h"
#include "../core/SystemSupervisor.h"
#include "../core/EventLog.h"

namespace hp {

MediaSessionService& MediaSessionService::instance() {
    static MediaSessionService m;
    return m;
}

bool MediaSessionService::ensureLock() {
    if (!lock_) lock_ = xSemaphoreCreateRecursiveMutex();
    return lock_ != nullptr;
}

int MediaSessionService::indexFor(uint32_t id) const {
    for (size_t i = 0; i < count_; ++i) if (sources_[i].id == id) return (int)i;
    return -1;
}

uint32_t MediaSessionService::registerSource(MediaSourceType type, const char* name, MediaCommandHandler handler) {
    if (!ensureLock()) return 0;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    if (count_ >= kMaxSources) {
        xSemaphoreGiveRecursive(lock_);
        return 0;
    }
    Source& s = sources_[count_++];
    s.id = nextId_++;
    if (!s.id) s.id = nextId_++;
    s.type = type;
    s.handler = handler;
    strlcpy(s.name, name ? name : "media", sizeof(s.name));
    s.state.type = type;
    s.state.sourceId = s.id;
    const uint32_t id = s.id;
    xSemaphoreGiveRecursive(lock_);
    return id;
}

bool MediaSessionService::preparePlayback(uint32_t sourceId) {
    if (!sourceId) return false;
    return requestAudioFocus(sourceId);
}

bool MediaSessionService::requestAudioFocus(uint32_t sourceId, bool pausePrevious) {
    if (!ensureLock()) return false;

    MediaCommandHandler previousHandler = nullptr;
    uint32_t previousSource = 0;
    bool previousPlaying = false;

    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    if (indexFor(sourceId) < 0) {
        xSemaphoreGiveRecursive(lock_);
        return false;
    }
    if (focusedSource_ == sourceId) {
        activeSource_ = sourceId;
        focusState_ = AudioFocusState::Held;
        xSemaphoreGiveRecursive(lock_);
        return true;
    }

    focusState_ = AudioFocusState::Switching;
    previousSource = focusedSource_;
    if (previousSource) {
        const int i = indexFor(previousSource);
        if (i >= 0) { previousHandler = sources_[i].handler;previousPlaying=sources_[i].state.playing; }
    }
    xSemaphoreGiveRecursive(lock_);

    // Critical rule: never call an app handler while holding the global media lock.
    const bool stopped = !previousHandler || (pausePrevious ?
        (!previousPlaying || previousHandler(MediaCommand::PlayPause)) :
        previousHandler(MediaCommand::Stop));
    if (!stopped) {
        EventLog::instance().add("WARN", "AUDIO_FOCUS_STOP",
                                 "Previous media source did not confirm stop");
        xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
        focusState_ = focusedSource_ ? AudioFocusState::Held : AudioFocusState::Idle;
        xSemaphoreGiveRecursive(lock_);
        return false;
    }

    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    if (indexFor(sourceId) < 0) {
        focusState_ = AudioFocusState::Idle;
        xSemaphoreGiveRecursive(lock_);
        return false;
    }
    if (previousSource) {
        const int i = indexFor(previousSource);
        if (i >= 0) {
            sources_[i].state.playing = false;
            if(!pausePrevious)sources_[i].state.powered = false;
        }
    }
    focusedSource_ = sourceId;
    activeSource_ = sourceId;
    focusState_ = AudioFocusState::Held;
    xSemaphoreGiveRecursive(lock_);

    EventBus::instance().publish({EventType::MediaChanged, nullptr});
    return true;
}

void MediaSessionService::releaseAudioFocus(uint32_t sourceId) {
    if (!ensureLock()) return;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    if (focusedSource_ == sourceId) {
        focusedSource_ = 0;
        focusState_ = AudioFocusState::Idle;
        const int i = indexFor(sourceId);
        if (i >= 0) sources_[i].state.playing = false;
        chooseFallbackLocked();
    }
    xSemaphoreGiveRecursive(lock_);
    EventBus::instance().publish({EventType::MediaChanged, nullptr});
}

void MediaSessionService::chooseFallbackLocked() {
    if (focusedSource_ && indexFor(focusedSource_) >= 0) {
        activeSource_ = focusedSource_;
        return;
    }
    if (activeSource_ && indexFor(activeSource_) >= 0) {
        const auto& s = sources_[indexFor(activeSource_)].state;
        if (s.available) return;
    }
    activeSource_ = 0;
    for (size_t i = 0; i < count_; ++i) {
        if (sources_[i].state.available && sources_[i].state.playing) {
            activeSource_ = sources_[i].id;
            return;
        }
    }
    for (size_t i = 0; i < count_; ++i) {
        if (sources_[i].state.available) {
            activeSource_ = sources_[i].id;
            return;
        }
    }
}

void MediaSessionService::update(uint32_t id, const MediaState& input) {
    SystemSupervisor::instance().heartbeat(ServiceId::Media);
    if (!ensureLock()) return;

    bool wantsFocus = false;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    const int i = indexFor(id);
    if (i >= 0) {
        MediaState s = input;
        s.sourceId = id;
        s.type = sources_[i].type;
        if (!s.source.length()) s.source = sources_[i].name;
        wantsFocus = s.playing && focusedSource_ != id;
        sources_[i].state = s;
        if (!s.available && activeSource_ == id) chooseFallbackLocked();
    }
    xSemaphoreGiveRecursive(lock_);

    if (wantsFocus && !requestAudioFocus(id)) {
        xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
        const int failed = indexFor(id);
        if (failed >= 0) sources_[failed].state.playing = false;
        xSemaphoreGiveRecursive(lock_);
        EventLog::instance().add("WARN", "MEDIA_FOCUS_DENIED",
                                 String("Source ") + String(id) + " could not acquire audio focus");
    }
    EventBus::instance().publish({EventType::MediaChanged, nullptr});
}

void MediaSessionService::setHandler(MediaCommandHandler h) {
    if (!legacySource_) legacySource_ = registerSource(MediaSourceType::None, "legacy", h);
    else if (ensureLock()) {
        xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
        const int i = indexFor(legacySource_);
        if (i >= 0) sources_[i].handler = h;
        xSemaphoreGiveRecursive(lock_);
    }
}

void MediaSessionService::update(const MediaState& s) {
    if (!legacySource_) legacySource_ = registerSource(s.type, "legacy", nullptr);
    update(legacySource_, s);
}

MediaState MediaSessionService::snapshot() {
    SystemSupervisor::instance().heartbeat(ServiceId::Media);
    MediaState out;
    if (!ensureLock()) return out;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    chooseFallbackLocked();
    const int i = indexFor(activeSource_);
    if (i >= 0) out = sources_[i].state;
    xSemaphoreGiveRecursive(lock_);
    return out;
}

bool MediaSessionService::trySnapshot(MediaState& out) {
    SystemSupervisor::instance().heartbeat(ServiceId::Media);
    if (!ensureLock() || xSemaphoreTakeRecursive(lock_, 0) != pdTRUE) return false;
    chooseFallbackLocked();
    const int i = indexFor(activeSource_);
    if (i >= 0) out = sources_[i].state;
    xSemaphoreGiveRecursive(lock_);
    return true;
}

int MediaSessionService::volumePercent(const MediaState& state) {
    if (state.volumeMax <= 0) return 0;
    const int value = constrain(state.volume, 0, state.volumeMax);
    return constrain((value * 100 + state.volumeMax / 2) / state.volumeMax, 0, 100);
}

MediaPresentationState MediaSessionService::presentation() {
    MediaPresentationState out;
    out.media = snapshot();
    out.volumePercent = volumePercent(out.media);
    switch (focusState_) {
        case AudioFocusState::Held: out.focus = "held"; break;
        case AudioFocusState::Switching: out.focus = "switching"; break;
        default: out.focus = "idle"; break;
    }
    return out;
}

bool MediaSessionService::commandSource(uint32_t sourceId, MediaCommand c) {
    if(SecurityService::instance().tabletLocked())return false;
    if (!ensureLock() || !sourceId) return false;

    MediaCommandHandler handler = nullptr;
    MediaState current;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    const int i = indexFor(sourceId);
    if (i >= 0) {
        handler = sources_[i].handler;
        current = sources_[i].state;
    }
    xSemaphoreGiveRecursive(lock_);
    if (!handler) return false;

    const bool canStart =
        c == MediaCommand::PlayPause ||
        c == MediaCommand::PowerToggle;
    if (canStart && !current.playing && !preparePlayback(sourceId)) return false;

    const bool ok = handler(c);
    if (ok && c == MediaCommand::Stop) releaseAudioFocus(sourceId);
    return ok;
}

bool MediaSessionService::command(MediaCommand c) {
    if (!ensureLock()) return false;
    uint32_t source = 0;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    chooseFallbackLocked();
    source = activeSource_;
    xSemaphoreGiveRecursive(lock_);
    return commandSource(source, c);
}

}
