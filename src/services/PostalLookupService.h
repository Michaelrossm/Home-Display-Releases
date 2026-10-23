#pragma once
#include <ArduinoJson.h>
#include <Arduino.h>
#include <atomic>
namespace hp {
// Single worker-only provider for all apps. Input: postal_code + ISO country.
// Output: places[{city,region,country,country_code,latitude?,longitude?}].
// Only country/PLZ leave the device; no names/credentials/full addresses.
bool lookupPostalCode(JsonVariantConst args, JsonDocument& result, String& error,
                      const std::atomic<bool>* cancelled);
}
