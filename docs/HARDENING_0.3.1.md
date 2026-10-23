# Hardening — v0.3.1

- Added central exclusive audio focus.
- Explicit Stop contract prevents simultaneous radio/music/NAS playback.
- BLE capability is reported truthfully; no fake A2DP support on ESP32-C6.
- External Bluetooth-audio adapter boundary prepared.
- NAS credentials separated from profile metadata.
- WebDAV core transport reuses existing network stack.
- SMB remains optional rather than adding an LGPL dependency blindly.
- Drag/drop uploads stream in 4 KiB chunks.
- Upload commit uses `.part` + rename.
- NAS/SD/radio all converge on one MediaSession and one output path.
