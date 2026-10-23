# Waveshare BSP integration — v0.3.3

Pinned official component:
`waveshare/esp32_p4_platform` 2.0.1.

Used for:
- 10.1" JD9365 MIPI-DSI display
- GT911 touch
- DSI PHY power
- backlight/brightness
- LVGL adapter
- ES8311 audio
- official SDMMC pinout

Home-Display still owns apps, services, storage policy, networking, media focus,
OTA/recovery, settings/secrets and diagnostics.

The BSP LVGL adapter owns the LVGL service task, so Home-Display does not run a
parallel LVGL timer in BSP mode. Main-task UI changes use the BSP display mutex.

SD remains exposed through Arduino `SD_MMC` to preserve the existing StorageService
FS abstraction, but pins are now official Waveshare values:
D0..D3 = 39..42, CLK=43, CMD=44.

Automatic formatting on mount failure stays disabled.
