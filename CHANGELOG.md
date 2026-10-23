# Changelog

## 2026-10-17 — VNext rendering, media and repository maintenance

- Shared RGB565/JPEG/PNG/WebP image pipeline, bounded photo preview/prefetch,
  fullscreen gestures and confirmed deletion through central storage services.
- Corrected pixelated resampling: area-filtered PNG, bilinear RGB565 resizing,
  static fullscreen antialiasing; retained direct rotation without resampling.
- Size-aware printer thumbnails, adaptive widget refresh caps and diagnostics.
- Sequential image/app-package batch uploads with existing installer safety.
- German language packs and common SD/USB language-package infrastructure.
- Removed 45 obsolete patch/install files, superseded reports and unused native
  Strom/diagnostics shells. Shared services, recovery and personal data retained.
- Consolidated README, documentation index, validation record and project metadata;
  corrected current 9-MiB OTA partition descriptions and Legacy build default.
- No partition migration, automatic formatting or device flashing performed.

## 0.3.3b — Windows Component Manager lock hardening

- Added `python tools/dev.py deepclean`.
- Added Windows PowerShell cleanup helper for stale CMake/Ninja/PlatformIO handles.
- Clean removes only generated build/component state, never source/user data.
- Added guidance for WinError 32 under `managed_components`.
- Source/build configuration otherwise unchanged.

## 0.3.3a — Hybrid FreeRTOS/Kconfig hotfix

- Set `CONFIG_FREERTOS_HZ=1000` as required by Arduino-ESP32 3.3.11.
- Removed invalid `CONFIG_ESP32P4_SPIRAM_SUPPORT` defaults that generated warnings.
- Let Waveshare BSP derive 400 kHz I2C speed from `BSP_I2C_FAST_MODE` instead of forcing a disabled symbol.
- No partition or application architecture changes.

## 0.3.3-dev — Official Waveshare BSP integration

- Fixed hybrid build component resolution by simplifying src/CMakeLists.
- Added official Waveshare ESP32-P4 Platform BSP 2.0.1.
- Pinned one LVGL 9.3.0 IDF component and removed duplicate PlatformIO LVGL/GFX libs.
- Display/GT911/backlight now use the official Waveshare BSP.
- Added BSP-aware LVGL locking and removed parallel timer handling.
- ES8311 output now uses Waveshare BSP + esp_codec_dev.
- SDMMC now uses official Waveshare GPIO39..44 pinout.
- Automatic storage formatting remains disabled.

## 0.3.3 — ESP-IDF CMake hotfix

- Removed `CONFIGURE_DEPENDS` from `src/CMakeLists.txt`.
- Fixes ESP-IDF `component_get_requirements.cmake` script-mode failure.
- Added regression test so this invalid flag cannot be reintroduced unnoticed.
- No firmware architecture, partitions, board settings or runtime behavior changed.

## 0.3.3-dev — Codex / PlatformIO ready

- Added project-local 32-MB Waveshare P4 board definitions.
- Added rev3.x/pre-v3 sdkconfig overlays and dev/prod overlays.
- Added hybrid Arduino + ESP-IDF CMake entry points.
- Added canonical `tools/dev.py` workflow.
- Added PlatformIO bootstrap scripts.
- Added committed VS Code settings/tasks/extensions.
- Added `AGENTS.md` and `CODEX_HANDOFF.md`.
- Added verified Waveshare display/audio constants and reference notes.
- CI now uses the same developer commands as Codex.

## 0.3.3-dev — Audio focus / BLE / NAS / file transfer

- Added global exclusive audio focus and explicit Stop command.
- Prevents simultaneous music/radio/NAS playback.
- Added BLE manager architecture for the ESP32-C6.
- Classic Bluetooth/A2DP is explicitly marked unavailable onboard.
- Added NAS profiles with WebDAV core and DLNA/SMB adapter boundaries.
- NAS passwords use internal secret storage.
- Added drag-and-drop file upload on port 8088 with atomic commit.

## 0.3.0 — IR-Tracker architecture hardening

- Central ConfigService + schema migration; v0.2.7 preserved.
- Hardened StorageService with non-destructive mount and atomic backup recovery.
- Added BootManager, SystemSupervisor, BootHealthCheck and embedded RecoveryUI.
- Replaced 30-second OTA acceptance with mandatory local health commit.
- Added dual-record TransactionJournal outside replaceable assets.
- Added signed UpdateVerifier and transactional firmware/SystemData/AppFS UpdateService.
- Added verified SD asset backups before erase.
- Adapted IR EventLog to P4 with PSRAM-preferred 512-record ring.
- Replaced ESPmDNS with adapted MinimalMdns for `_http` and `_homedisplay`.
- Added NetworkWorker and retry/backoff; browser no longer blocks UI.
- Added DiagnosticsService/DiagnosticsApp and secret-redacted support report.
- Added WebApp schema 2, permissions and legacy migration.
- Removed unused legacy radio storage/discovery modules.
- Added dev/prod build variants, asset pipeline, signed release tools and release validator.

## 0.2.7-dev — Home-Display / Music / Web Apps

### Product
- Product/UI name changed to **Home-Display**.
- Internal storage paths remain `/homepanel/...` for upgrade compatibility.

### Music
- Added Music app, SD library scanner and shared MusicPlayerService.
- Added Apache-2.0 `esp-audio-player 1.1.0` and `esp-libhelix-mp3 1.0.3` component dependencies.
- PlatformIO moves to hybrid `arduino, espidf` to support IDF components cleanly.
- Music publishes the existing MediaSessionService, so screensaver controls work.
- Audio hardware details are isolated in AudioHardwareService.
- Exact Waveshare D-board ES8311 BSP remains the last hardware-specific audio port.

### Web Apps
- Added MiniBrowser.
- Added saved web apps ("Zum Home-Bildschirm") with up to 16 entries.
- Added local-only URL policy by default.
- Added 128-KiB page cap.
- Saved web pages appear as desktop app tiles.
- No fake Chromium/JavaScript support; native adapters are the intended path for rich local device UIs.

### Hardware
- Added HardwareCapabilitiesService.
- Added AmbientLightService and auto-brightness policy hook.
- Confirmed no documented onboard ambient-light sensor; external I2C ALS is supported by architecture.

## 0.2.6-dev — Modern UI / wallpaper patch

### Modern desktop
- Added central UiTheme.
- Added dark gradient fallback background.
- Added optional SD JPG wallpaper support.
- Added wallpaper dim/scrim configuration.
- Added modern translucent top/status bar.
- Added large rounded application cards.
- Added central IconCatalog.
- Settings now uses a real gear symbol.
- Radio, Power, Calendar, Weather and Home use consistent LVGL symbols.
- Added LAN/Wi-Fi and SD indicators to the status bar.
- Added Home button with proper symbol.
- Reworked screensaver media controls to proper icon buttons.

### Image/performance
- Enabled LVGL TJPGD/JPG decoder.
- PNG stays disabled by default to save flash/RAM.
- Added read-only LVGL filesystem bridge over existing SD_MMC.
- Wallpaper decoding gets a short PerformanceGuard.
- App content roots are transparent and reuse the shared desktop background.
- Missing/broken wallpaper automatically falls back to built-in theme.

## 0.2.5-dev — Calendar + performance hardening

### Family calendar
- Added Calendar app and central CalendarService.
- E-mail is the canonical family-person identity.
- Designed for at least 4 people; supports up to 16.
- Added shared family calendar and virtual birthdays calendar.
- One event can belong to multiple people through a 16-bit participant mask.
- Birthdays remain stored only in PersonRecord.
- Added compact binary persons/calendars/events/meta stores.
- Added 32-item upcoming-event RAM cache.
- Added CalDAV-ready UID, ETag, href and sync-revision model.
- Added provider model for local, generic CalDAV, Google CalDAV and iCloud.
- Added local credential service with salted iterated SHA-256 password hashes.

### Performance / memory
- Calendar uses no dedicated FreeRTOS task.
- Calendar and sync join the central BackgroundScheduler.
- Shared background worker stack reduced 10 KiB -> 8 KiB.
- EventBus fixed capacity reduced 20 -> 16.
- No full event database is loaded into RAM.
- Fixed binary storage helpers added to StorageService.

### Reliability
- Fixed TimeService begin return-type/startup mismatch.
- Calendar write limits and e-mail validation added.
- CalDAV transport is explicitly kept disabled until HTTPS/WebDAV routing is implemented.

## 0.2.4-dev

### Adaptive Eco
- Eco is no longer a hard 180/200 MHz cap.
- Correct P4 low stages: 40 MHz then 90 MHz pre-v3 / 100 MHz v3.x.
- Added Deep Eco at 40 MHz.
- Added Eco Cruise at 40..90/100 MHz.
- Touch/UI/network activity exposes the full 360/400 MHz ceiling temporarily.
- PerformanceGuard pins full performance only for short heavy sections.
- Radio, weather and power-tracker network work uses short guarded boosts.

### Network Eco
- LAN remains preferred.
- Stable LAN switches Wi-Fi OFF completely by default.
- LAN loss automatically re-enables Wi-Fi fallback.
- Active Wi-Fi uses modem sleep.
- MIN_MODEM is the default for responsiveness.
- Optional MAX_MODEM only in deep idle.
- Adaptive C6 TX ceiling based on RSSI: 11/15/20 dBm.
- Apps cannot own Wi-Fi power policy.

## 0.2.3-dev

### Energy saving
- Added central `PowerManager`.
- Enabled ESP-IDF dynamic frequency scaling (DFS).
- Default CPU range is 40 MHz to the silicon maximum.
- Legacy P4 profile uses 360/180 MHz full/eco ceilings.
- P4 v3.x profile uses 400/200 MHz full/eco ceilings.
- Dim, screensaver and display-off states automatically use the Eco ceiling.
- Energy saving can be switched on/off in Settings.
- Added reference-counted `PerformanceGuard` for short CPU-heavy work.
- Background scheduler wake interval now scales with idle state.
- LVGL servicing rate now scales with idle state.
- Main-loop wake rate is reduced while idle.
- Automatic Light-sleep is prepared but deliberately disabled until exact board
  wake behaviour is qualified.

### Architecture
- CPU frequency policy exists only in `PowerManager`; apps do not set MHz directly.
- Existing screensaver/idle service remains the source of user-idle state.
- No additional per-app power tasks were introduced.

## 0.2.1-dev

### Storage
- SD is now the primary user-data location.
- Internal `userdata` partition removed.
- New layout: 11 MiB A + 11 MiB B + 7 MiB AppFS + 2.8125 MiB SystemData.
- Added centralized storage classes and an atomic small-file write helper.
- Media and voice models require SD instead of silently filling internal flash.

### Updates
- Clarified that OTA works identically over LAN or Wi-Fi.
- Ethernet is the preferred network path.
- Added pending-image health validation and delayed OTA confirmation.
- Added rollback/certificate-bundle sdkconfig defaults for compatible build paths.

### Reliability
- Duplicate AppManager/EventBus/BackgroundScheduler registrations rejected.
- Settings are sanitized before use/storage.
- mDNS restarts correctly after network-path changes.
- Startup only arms OTA validation after core/apps/background/UI succeed.

### Architecture
- Old monolithic code remains excluded.
- Radio, weather and power tracker stay separate apps.
- Screensaver/idle stays a system service and communicates through shared media state.
