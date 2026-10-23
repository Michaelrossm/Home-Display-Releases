# Bluetooth — v0.3.1

The Waveshare board uses an ESP32-C6 wireless coprocessor.

ESP32-C6 provides Bluetooth LE, but not Classic Bluetooth/A2DP.

Home-Display therefore prepares:
- BLE discovery
- BLE pairing/bonding state
- BLE sensors/peripherals
- future GATT integrations

Onboard hardware does **not** claim:
- A2DP headphones
- A2DP speakers
- Classic Bluetooth file transfer

A future external Classic-Bluetooth or LE-Audio capable coprocessor can register as
one media source and must obey the same global audio-focus rules.
