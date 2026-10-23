# HomePanel v0.2 architecture

## Fixed rules
- One owner per capability. No app owns Wi-Fi, Ethernet, mDNS, SD, NVS, display/backlight, audio or OTA.
- Apps own only their domain logic/UI.
- One shared background task services radio, weather and power jobs to avoid one large FreeRTOS stack per app.
- Screensaver/idle is a system service, not an app.
- MediaSessionService is the bridge between the screensaver/system UI and music/radio apps.
- Landscape 1280x800 is default; rotation is one global setting.
- Optional SD is never required to boot.
- Dynamic apps are prepared but disabled until the stable App API is frozen.

## Imported from the old WT32 firmware
Kept: FSAPI/UNDOK radio engine, radio discovery/favorites helpers, Open-Meteo client, IR tracker power client, behavior of the 3-stage idle profiles.

Not kept: monolithic FitzTouchUI, old 480x320 layout, old main task/global-state layout, WT32 display driver, obsolete 4MB partitioning.
