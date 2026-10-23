#pragma once
#include <Arduino.h>

namespace hp {

struct HardwareCapabilities {
    bool onboardMicrophone = true;
    bool onboardAudioCodec = true;
    bool speakerOutput = true;
    bool ethernet = true;
    bool poe = true;
    bool wifi6 = true;
    bool bluetoothLe = true;
    bool bluetoothClassicAudio = false;
    bool bluetoothExternalAudioAdapterSupported = true;
    bool sdmmc = true;
    bool usbOtg = true;
    bool mipiDsi = true;
    bool mipiCsi = true;

    // Waveshare does not document an onboard ambient-light sensor on this board.
    bool onboardAmbientLight = false;
    bool externalI2cAmbientLightSupported = true;
};

class HardwareCapabilitiesService {
public:
    static HardwareCapabilitiesService& instance();
    const HardwareCapabilities& capabilities() const { return caps_; }

private:
    HardwareCapabilities caps_{};
};

}
