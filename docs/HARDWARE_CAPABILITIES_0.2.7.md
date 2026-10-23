# Waveshare hardware capabilities used by Home-Display

For ESP32-P4-WIFI6-POE-ETH-D the documented board features include:

- ESP32-P4 + ESP32-C6 Wi-Fi 6/BLE
- 32 MB PSRAM and 32 MB flash
- MIPI-DSI display
- MIPI-CSI camera connector
- SDIO 3.0 TF/microSD
- 100-Mbit Ethernet / PoE
- USB OTG
- onboard SMD microphone
- audio codec + amplifier
- speaker connector, 8 ohm / 2 W
- external I2C header

Waveshare does **not** document an onboard ambient-light sensor for this board.

Home-Display therefore:
- does not fake an ALS;
- exposes `AmbientLightService`;
- supports automatic brightness once an external I2C lux driver reports values;
- keeps the brightness policy independent of the specific sensor model.

A later BH1750/VEML7700/etc add-on can be added without changing the UI/Idle system.
