#pragma once
#include <Arduino.h>

namespace hp {

// Radio-owned configuration.  Persistence stays in ConfigService so there is
// one encrypted credential store, while the schema and its UI belong to Radio.
struct RadioAppSettings {
    String host;
    String pin = "1234";
    bool parentalEnabled = false;
    bool volumeLimitEnabled = false;
    uint8_t maxVolumePercent = 60;
    bool lockSources = false;
    bool lockPowerOff = false;
    bool lockDisplayControls = false;
};

class RadioSettings {
public:
    static RadioSettings& instance();
    RadioAppSettings load() const;
    bool save(const RadioAppSettings& settings) const;
};

}
