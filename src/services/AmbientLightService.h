#pragma once
#include <Arduino.h>

namespace hp {

enum class AmbientLightSource : uint8_t {
    None,
    ExternalI2C
};

struct AmbientLightStatus {
    AmbientLightSource source = AmbientLightSource::None;
    bool available = false;
    bool autoBrightnessActive = false;
    float lux = 0.0f;
    uint8_t targetBrightness = 0;
};

class AmbientLightService {
public:
    static AmbientLightService& instance();

    bool begin();
    void loop();
    AmbientLightStatus status() const { return status_; }

    // Board/add-on driver reports lux here. This keeps the policy independent
    // from a specific BH1750/VEML7700/etc driver.
    void reportLux(float lux, AmbientLightSource source = AmbientLightSource::ExternalI2C);
    void sensorUnavailable();

private:
    uint8_t brightnessForLux(float lux) const;
    AmbientLightStatus status_{};
    uint32_t lastApply_ = 0;
};

}
