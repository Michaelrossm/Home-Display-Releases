#pragma once
#include <Arduino.h>

namespace hp {
struct RadioDiscoveryDevice { char host[64]{}; char name[96]{}; bool verified=false; };

class RadioDiscovery {
public:
    static int discover(RadioDiscoveryDevice* out, int capacity, const String& pin, uint16_t timeoutMs=2600);
};
}
