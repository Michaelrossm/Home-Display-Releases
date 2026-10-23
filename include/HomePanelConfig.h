#pragma once
#include "BuildInfo.h"

#ifndef HOMEPANEL_VERSION
#define HOMEPANEL_VERSION hp::build::kVersion
#endif

#ifndef HOMEPANEL_HOSTNAME
#define HOMEPANEL_HOSTNAME "homepanel"
#endif

#ifndef HOMEPANEL_DISPLAY_WIDTH
#define HOMEPANEL_DISPLAY_WIDTH 1280
#endif

#ifndef HOMEPANEL_DISPLAY_HEIGHT
#define HOMEPANEL_DISPLAY_HEIGHT 800
#endif

#ifndef HOMEPANEL_REQUIRE_HTTPS_OTA
#define HOMEPANEL_REQUIRE_HTTPS_OTA 1
#endif

namespace hp {
inline constexpr const char* kProduct = build::kProduct;
inline constexpr const char* kVersion = build::kVersion;
inline constexpr const char* kDefaultHostname = HOMEPANEL_HOSTNAME;
inline constexpr int kWidth = HOMEPANEL_DISPLAY_WIDTH;
inline constexpr int kHeight = HOMEPANEL_DISPLAY_HEIGHT;

// Keep paths centralized. Apps must not invent their own storage roots.
inline constexpr const char* kSdRoot = "/homepanel";
inline constexpr const char* kInternalFallbackRoot = "/fallback";
}


#ifndef HOMEPANEL_FEATURE_POWER_MANAGEMENT
#define HOMEPANEL_FEATURE_POWER_MANAGEMENT 1
#endif

// Keep automatic Light-sleep disabled until DSI/touch, IP101 Ethernet,
// SDMMC and ESP32-C6 wake behaviour has been qualified on the exact board.
#ifndef HOMEPANEL_POWER_ALLOW_LIGHT_SLEEP
#define HOMEPANEL_POWER_ALLOW_LIGHT_SLEEP 0
#endif


#ifndef HOMEPANEL_FEATURE_CALENDAR
#define HOMEPANEL_FEATURE_CALENDAR 1
#endif

#ifndef HOMEPANEL_CALENDAR_MAX_PERSONS
#define HOMEPANEL_CALENDAR_MAX_PERSONS 16
#endif

#ifndef HOMEPANEL_CALENDAR_MAX_CALENDARS
#define HOMEPANEL_CALENDAR_MAX_CALENDARS 32
#endif

#ifndef HOMEPANEL_CALENDAR_UPCOMING_CACHE
#define HOMEPANEL_CALENDAR_UPCOMING_CACHE 32
#endif


#ifndef HOMEPANEL_FEATURE_WALLPAPER
#define HOMEPANEL_FEATURE_WALLPAPER 1
#endif


#ifndef HOMEPANEL_FEATURE_MUSIC
#define HOMEPANEL_FEATURE_MUSIC 1
#endif
#ifndef HOMEPANEL_FEATURE_WEBAPPS
#define HOMEPANEL_FEATURE_WEBAPPS 1
#endif
#ifndef HOMEPANEL_FEATURE_AMBIENT_LIGHT
#define HOMEPANEL_FEATURE_AMBIENT_LIGHT 1
#endif
#ifndef HOMEPANEL_AUDIO_BACKEND_ESP_AUDIO_PLAYER
#define HOMEPANEL_AUDIO_BACKEND_ESP_AUDIO_PLAYER 1
#endif
