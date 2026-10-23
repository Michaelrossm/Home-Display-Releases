#pragma once
#include <Arduino.h>

namespace hp {

struct GoliathAppSettings {
    bool enabled = false;
    String host;
    uint16_t httpPort = 80;
    uint16_t rtspPort = 554;
    String username;
    String password;
    String apartment;
    String indoorStation;
    bool useExtraStream = true;
    bool showCameraOnRing = true;
    bool diagnosticsEnabled = false;
};

// App-owned schema. Persistence deliberately remains in ConfigService so
// credentials retain the project's single protected storage path.
class GoliathSettings {
public:
    static GoliathSettings& instance();
    GoliathAppSettings load() const;
    bool save(const GoliathAppSettings& settings) const;
};

}
