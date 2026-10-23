#include "AmbientLightService.h"
#include "SettingsService.h"
#include "IdleService.h"
#include "../hardware/DisplayManager.h"

namespace hp {

AmbientLightService& AmbientLightService::instance() {
    static AmbientLightService a;
    return a;
}

bool AmbientLightService::begin() {
    // No onboard ALS exists according to the board documentation.
    status_.available = false;
    status_.source = AmbientLightSource::None;
    return true;
}

void AmbientLightService::reportLux(float lux, AmbientLightSource source) {
    if (!isfinite(lux) || lux < 0.0f || lux > 200000.0f) return;
    status_.lux = lux;
    status_.source = source;
    status_.available = true;
}

void AmbientLightService::sensorUnavailable() {
    status_.available = false;
    status_.source = AmbientLightSource::None;
}

uint8_t AmbientLightService::brightnessForLux(float lux) const {
    const auto s = SettingsService::instance().snapshot().ambientLight;
    if (lux <= s.darkLux) return s.minBrightness;
    if (lux >= s.brightLux) return s.maxBrightness;

    const float span = max<int>(1, s.brightLux - s.darkLux);
    const float t = (lux - s.darkLux) / span;
    return (uint8_t)(
        s.minBrightness +
        t * (float)(s.maxBrightness - s.minBrightness)
    );
}

void AmbientLightService::loop() {
    if (millis() - lastApply_ < 1000) return;
    lastApply_ = millis();

    const auto settings = SettingsService::instance().snapshot();
    status_.autoBrightnessActive =
        settings.ambientLight.autoBrightness && status_.available;

    if (!status_.autoBrightnessActive) return;
    if (IdleService::instance().stage() != IdleStage::Active) return;

    const uint8_t target = brightnessForLux(status_.lux);
    status_.targetBrightness = target;
    // UiShell::applyIdle() owns the physical backlight for all idle stages.
}

}
