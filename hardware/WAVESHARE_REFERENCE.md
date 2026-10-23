# Waveshare reference — Home-Display v0.3.2

Official upstream: `waveshareteam/ESP32-P4-Platform`

Current upstream CI documents ESP-IDF 5.5.5/6.0.2 and Arduino-ESP32 3.3.11.
Home-Display pins pioarduino 55.03.311 / Arduino 3.3.11 / IDF 5.5.5.

Display/touch references:
- examples/arduino/examples/Drawing_board/Drawing_board.ino
- examples/arduino/libraries/displays/displays_config.h
- examples/arduino/libraries/displays/gt911.*

Selected: SCREEN_10_1_DSI_TOUCH_A.
Verified values are centralized in include/BoardConfig.h.
The full vendor init-command array is intentionally not duplicated: port it from
current upstream and preserve upstream notice/license.

Audio:
- examples/arduino/examples/Audio_Playback/Audio_Playback.ino
- ES8311 0x18
- PA GPIO53
- I2S MCLK/BCLK/LRCK/DOUT 13/12/10/9
- I2C SDA/SCL 7/8

Bring-up:
00_board_check -> display/touch -> 09_sdmmc -> 11_ethernetbasic ->
10_wifistation -> 12_I2SCodec -> 14_lvgl_demo_v9 -> 19_system_monitor
