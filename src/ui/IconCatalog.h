#pragma once
#include <lvgl.h>
#include <cstring>

namespace hp {

class IconCatalog {
public:
    static const char* forApp(const char* id) {
        if (!id) return LV_SYMBOL_LIST;
        struct Mapping { const char* name; const char* symbol; };
        static const Mapping icons[] = {
            {"energy",LV_SYMBOL_CHARGE},{"solar",LV_SYMBOL_IMAGE},{"battery",LV_SYMBOL_BATTERY_FULL},
            {"grid",LV_SYMBOL_CHARGE},{"house",LV_SYMBOL_HOME},{"car",LV_SYMBOL_DRIVE},
            {"heatpump",LV_SYMBOL_SETTINGS},{"wifi",LV_SYMBOL_WIFI},{"ethernet",LV_SYMBOL_DRIVE},
            {"warning",LV_SYMBOL_WARNING},{"info",LV_SYMBOL_LIST},{"audio",LV_SYMBOL_AUDIO},
            {"play",LV_SYMBOL_PLAY},{"pause",LV_SYMBOL_PAUSE},{"next",LV_SYMBOL_NEXT},
            {"previous",LV_SYMBOL_PREV},{"refresh",LV_SYMBOL_REFRESH},

            // Semantic weather names use glyphs already present in the bounded
            // Noto Emoji UI fonts, so this adds no new font bitmap storage.
            {"clear","☀"},{"sun","☀"},{"clear_night","🌙"},{"moon","🌙"},
            {"partly_cloudy","⛅"},{"cloudy","☁"},
            {"drizzle","🌦"},{"rain","🌧"},{"heavy_rain","🌧"},
            {"storm","⛈"},{"thunderstorm","⛈"},{"snow","🌨"},{"snowflake","❄"},
            {"wind","💨"},{"temperature","🌡"},{"umbrella","☔"},{"rainbow","🌈"},
            // Static fallback only. hp_ui_weather_effect("fog") renders proper
            // moving fog bands and should be preferred for weather cards.
            {"fog","☁"}
        };
        for (const auto& icon:icons) if(std::strcmp(id,icon.name)==0)return icon.symbol;

        if (std::strcmp(id, "home") == 0) return LV_SYMBOL_HOME;
        if (std::strcmp(id, "settings") == 0) return LV_SYMBOL_SETTINGS;
        if (std::strcmp(id, "radio") == 0) return LV_SYMBOL_AUDIO;
        if (std::strcmp(id, "power") == 0) return LV_SYMBOL_CHARGE;
        if (std::strcmp(id, "calendar") == 0) return LV_SYMBOL_LIST;
        if (std::strcmp(id, "calculator") == 0) return LV_SYMBOL_PLUS;
        if (std::strcmp(id, "weather") == 0) return "⛅";
        if (std::strcmp(id, "goliath") == 0) return LV_SYMBOL_HOME;

        return LV_SYMBOL_LIST;
    }

    static const char* network(bool ethernet) {
        return ethernet ? LV_SYMBOL_DRIVE : LV_SYMBOL_WIFI;
    }

    static const char* sd() { return LV_SYMBOL_SD_CARD; }
    static const char* settings() { return LV_SYMBOL_SETTINGS; }
    static const char* home() { return LV_SYMBOL_HOME; }
};

}
