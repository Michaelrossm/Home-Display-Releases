# Remaining exact board-port work

The application architecture and imported app engines are complete, but exact low-level hardware initialization is deliberately isolated and not guessed. Before the first real display flash, copy/port the current official Waveshare profile for **ESP32-P4-WIFI6-POE-ETH-D + SCREEN_10_1_DSI_TOUCH_A** into `DisplayManager` and the hardware service layer.

Official Waveshare support covers this board for SDMMC, Wi-Fi, Ethernet, I2S codec, display and LVGL. The Ethernet PHY is IP101.

This isolation means the app code does not need another rewrite when the exact D-board revision is confirmed.
