#pragma once
#include <Arduino.h>
namespace hp { struct PowerDiscoveryDevice { char host[48]{}; char name[48]{}; }; class PowerDiscovery { public: static int discover(PowerDiscoveryDevice* out,int capacity,uint16_t timeoutMs=2200); }; }
