#pragma once
#include <Arduino.h>

namespace hp {

/*
 * Browser settings are deliberately separate from settings.lua.
 *
 * App package:
 *   app.json
 *   main.lua
 *   settings.lua        -> LVGL/display settings (optional)
 *   web_settings.json   -> browser settings description (optional)
 *
 * Both settings frontends use the same per-app KV/Secret namespaces.
 * No app supplied HTML or JavaScript is executed.
 */
class AppWebSettingsService {
public:
    static AppWebSettingsService& instance();

    // Builds a browser-neutral JSON model for WebServerService.
    bool describe(const String& appId, String& json, String& error);

    // Saves values from the browser model back into the normal app KV/Secret
    // namespace. Secrets are write-only and are never returned to the browser.
    bool apply(const String& body, String& json, String& error);
    // Read-only mDNS jobs on the existing NetworkWorker. POST start/poll both
    // require the normal authenticated, CSRF-protected web-settings route.
    bool discover(const String& appId,const String& field,uint32_t run,
                  String& json,String& error);

private:
    AppWebSettingsService() = default;
};

} // namespace hp
