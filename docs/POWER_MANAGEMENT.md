# Power management — v0.2.5

HomePanel uses the ESP32-P4 / ESP-IDF power-management subsystem instead of manually
changing CPU clocks throughout the applications.

## Default behaviour

Energy saving is ON by default.

### Active
- CPU range: 40 MHz to silicon maximum.
- Dynamic frequency scaling (DFS) chooses the required rate automatically.
- No app needs to micromanage CPU MHz.

### Dimmed / Screensaver / Display off
- CPU minimum remains 40 MHz.
- Maximum is capped to the P4's efficient intermediate PLL point:
  - pre-v3 / legacy P4: 180 MHz
  - v3.x P4: 200 MHz
- Background worker wake rate is reduced.
- LVGL update rate is reduced.

### Performance request
Short CPU-heavy operations can use:

```cpp
hp::PerformanceGuard boost;
```

While the guard exists, HomePanel restores the full CPU ceiling and holds
`ESP_PM_CPU_FREQ_MAX`. Destruction releases the lock automatically.

Typical candidates:
- image decoding;
- large JSON parsing;
- complex UI rebuild;
- future video/intercom setup;
- OTA verification.

Do not hold a performance lock while waiting for network I/O.

## Silicon revision

ESP-IDF's P4 power-management example currently exposes:
- older P4: 360 MHz max, 180 MHz intermediate;
- v3.x P4: 400 MHz max, 200 MHz intermediate.

The PlatformIO environment already separates `homepanel_p4_legacy` and
`homepanel_p4_r3`, so PowerManager follows that same profile.

## Light-sleep

Automatic Light-sleep is intentionally compiled OFF for now.

Reason: the final wake behaviour of MIPI-DSI/touch, IP101 Ethernet, SDMMC and the
ESP32-C6 hosted Wi-Fi link must first be tested on the exact D-board revision.

`PowerSettings::autoLightSleepWhenOff` and tickless idle are prepared so the feature
can be enabled later without redesigning the architecture.

This is deliberately safer than enabling Light-sleep before board qualification.
