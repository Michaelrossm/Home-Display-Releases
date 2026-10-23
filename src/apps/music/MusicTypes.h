#pragma once
#include <Arduino.h>

namespace hp {

enum class MusicState : uint8_t {
    Idle,
    Loading,
    Playing,
    Paused,
    Error
};

struct MusicTrack {
    char path[256]{};
    char title[72]{};
    char artist[56]{};
    uint32_t size = 0;
    uint64_t modified = 0;
};

struct MusicSnapshot {
    MusicState state = MusicState::Idle;
    uint8_t volume = 55;
    uint16_t index = 0;
    uint16_t count = 0;
    uint16_t queueCount = 0, queueIndex = 0;
    uint32_t revision = 0;
    bool scanning = false;
    bool shuffle = false;
    bool repeat = false;
    char title[72]{};
    char subtitle[96]{};
    char error[96]{};
};

}
