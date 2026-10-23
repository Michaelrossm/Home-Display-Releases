#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace hp {

class WebSettingsSchemaValidator {
public:
    static constexpr size_t kMaxBytes = 16384;
    static constexpr size_t kMaxTabs = 12;
    static constexpr size_t kMaxTopFields = 48;
    static constexpr size_t kMaxTotalFields = 96;
    static constexpr size_t kMaxOptions = 24;
    static constexpr size_t kMaxRepeaterItems = 16;
    static constexpr size_t kMaxRepeaterFields = 16;

    // Returns a stable error code in `error` on failure.
    static bool validate(JsonDocument& schema, String& error);
    static String message(const String& code);
};

} // namespace hp
