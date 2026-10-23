#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace hp {

enum class MediaSourceType : uint8_t {
    None, LocalFile, HttpStream, Radio, Dlna, Nas, BluetoothLeControl, WebApp, Intercom
};

enum class MediaCommand : uint8_t {
    PlayPause, Stop, Next, Previous, PowerToggle, MuteToggle, VolumeUp, VolumeDown
};

enum class AudioFocusState : uint8_t { Idle, Held, Switching };

struct MediaState {
    MediaSourceType type = MediaSourceType::None;
    uint32_t sourceId = 0;
    bool available = false;
    bool playing = false;
    bool powered = false;
    bool muted = false;
    int volume = 0;
    int volumeMax = 20;
    String title, subtitle, source;
};

struct MediaPresentationState {
    MediaState media;
    int volumePercent = 0;
    const char* focus = "idle";
};

using MediaCommandHandler = bool(*)(MediaCommand);

class MediaSessionService {
public:
    static MediaSessionService& instance();

    uint32_t registerSource(MediaSourceType type, const char* name, MediaCommandHandler handler);
    bool preparePlayback(uint32_t sourceId);
    bool requestAudioFocus(uint32_t sourceId, bool pausePrevious=false);
    void releaseAudioFocus(uint32_t sourceId);
    bool commandSource(uint32_t sourceId, MediaCommand c);

    void update(uint32_t sourceId, const MediaState& s);
    void update(const MediaState& s); // migration compatibility only
    MediaState snapshot();
    bool trySnapshot(MediaState& out);
    MediaPresentationState presentation();
    static int volumePercent(const MediaState& state);
    bool command(MediaCommand c);
    void setHandler(MediaCommandHandler h); // migration compatibility only

    uint32_t activeSource() const { return activeSource_; }
    uint32_t focusedSource() const { return focusedSource_; }
    AudioFocusState focusState() const { return focusState_; }

private:
    struct Source {
        uint32_t id = 0;
        MediaSourceType type = MediaSourceType::None;
        char name[32]{};
        MediaCommandHandler handler = nullptr;
        MediaState state;
    };

    bool ensureLock();
    int indexFor(uint32_t id) const;
    void chooseFallbackLocked();

    static constexpr size_t kMaxSources = 12;
    Source sources_[kMaxSources]{};
    size_t count_ = 0;
    uint32_t nextId_ = 1;
    uint32_t activeSource_ = 0;
    uint32_t focusedSource_ = 0;
    uint32_t legacySource_ = 0;
    AudioFocusState focusState_ = AudioFocusState::Idle;
    SemaphoreHandle_t lock_ = nullptr;
};

}
