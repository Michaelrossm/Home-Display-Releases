# Audio focus — v0.3.1

Home-Display permits exactly one audible media source at a time.

The central `MediaSessionService` owns audio focus for:
- local SD music
- HTTP/network stream
- radio
- NAS
- DLNA
- future external Bluetooth-audio adapter

When a source starts:
1. it requests audio focus;
2. the previous source receives `MediaCommand::Stop`;
3. focus transfer is rejected if the old source cannot confirm stop;
4. only then does the new source become active;
5. screensaver media controls follow the active source.

Apps must not open a second codec/output path behind MediaSessionService.
