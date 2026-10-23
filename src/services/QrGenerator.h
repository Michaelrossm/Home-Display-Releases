#pragma once
#include <Arduino.h>
#include <lvgl.h>
namespace hp {
// One shared facade over LVGL's existing QR encoder; no service task/cache.
class QrGenerator {
public:
    static constexpr size_t maxPayload = 2048;
    static bool matrix(const String& payload, int& size, String& bits);
    static bool update(lv_obj_t* qr, const String& payload);
    static String wifi(const String& ssid, const String& password);
};
}
