# Adaptive Eco — v0.2.7

Eco is the default behaviour and is not a permanent performance cap.

## ESP32-P4 CPU stages

The ESP32-P4 does not use an 80 MHz CPU operating point in the current supported
frequency table.

- pre-v3 P4: 40 -> 90 -> 180 -> 360 MHz
- P4 v3.x: 40 -> 100 -> 200 -> 400 MHz

HomePanel policy:

- Deep Eco: 40 MHz while screensaver/display-off and no work is pending.
- Eco Cruise: 40..90 MHz (pre-v3) or 40..100 MHz (v3.x).
- Adaptive Burst: 40..360/400 MHz after touch/UI/network activity.
- PerformanceGuard: short explicit max-frequency lock for heavy work.

Radio, weather and power-tracker network operations request full performance only
around the actual HTTP/parse work.

## ESP32-C6 Wi-Fi policy

- Ethernet stable -> Wi-Fi radio OFF completely by default.
- Ethernet lost -> Wi-Fi automatically starts as fallback.
- Wi-Fi active -> modem power saving.
- Default mode is WIFI_PS_MIN_MODEM.
- Optional aggressive mode can use WIFI_PS_MAX_MODEM only in deep idle.
- TX-power ceiling adapts every 30 seconds:
  - RSSI >= -55 dBm: 11 dBm request
  - RSSI -56 .. -67 dBm: 15 dBm request
  - RSSI < -67 dBm: 20 dBm request

TX power is a ceiling request; the C6 driver may use less.
