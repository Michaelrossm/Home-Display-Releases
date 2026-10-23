# Home-Display agent instructions

## First commands
```bash
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py build
```

Default build: `home_display_p4_legacy_dev` (360 MHz); R3 / 400 MHz must be selected explicitly for confirmed v3.x hardware.

## Target
Waveshare ESP32-P4-WIFI6-POE-ETH-D, 10.1-DSI-Touch-A, 32 MiB flash,
32 MiB PSRAM, native 800x1280, UI 1280x800.

- P4 v3.x: 400 MHz
- P4 pre-v3: 360 MHz
- Ethernet PHY family: IP101
- wireless coprocessor: ESP32-C6
- audio: ES8311

## Pinned stack
Do not casually upgrade while fixing app code:
- pioarduino 55.03.311
- Arduino-ESP32 3.3.11
- ESP-IDF 5.5.5
- LVGL 9.3.0
- ArduinoJson 7.4.3
- Arduino_GFX 1.6.0

## Architecture
1. Apps never access hardware directly.
2. Storage through StorageService.
3. Settings/secrets through central config/credential services.
4. Network operations through central services/worker.
5. One audible media source owns global audio focus.
6. No per-app FreeRTOS task without review.
7. One mDNS implementation.
8. Never bypass BootHealth/rollback.
9. Never auto-format damaged persistent storage.
10. Never guess unsupported hardware details.

## IR-Tracker reuse
Reference for tested solutions, not a C3 template.
Priority: robustness -> maintainability -> clear module boundaries -> diagnostics ->
performance -> reasonable memory -> minimum memory.
Use P4/PSRAM when clearer and safer.

## Hardware port
Verified constants live in `include/BoardConfig.h`.
Pending low-level ports must come from the current matching official Waveshare example.

## Revisions
Build output is not interchangeable:
- `home_display_p4_r3_*`: v3.x / 400 MHz
- `home_display_p4_legacy_*`: pre-v3 / 360 MHz

Unknown revision: do not flash.

## Completion
Always run:
```bash
python tools/dev.py test
python tools/dev.py validate
```
For compile changes:
```bash
python tools/dev.py build
```
Compilation is not hardware validation.

## ESP-IDF CMake requirement-scan rule

`src/CMakeLists.txt` is evaluated by ESP-IDF both during the normal configure phase
and during `component_get_requirements.cmake` script mode.

Do **not** use `CONFIGURE_DEPENDS` in `file(GLOB/GLOB_RECURSE ...)` there.
CMake rejects it in script mode and PlatformIO stops before compilation.

The project intentionally uses plain `GLOB_RECURSE` for component source discovery.


## Waveshare BSP integration

Preferred board layer: `waveshare/esp32_p4_platform` 2.0.1.

Do not duplicate JD9365, GT911 or ES8311 hardware drivers in app code.
Pioarduino hybrid mode provides Arduino itself; the main `src` IDF component only
registers Home-Display sources.
When the Waveshare LVGL adapter is active, do not run another LVGL timer task.

## FreeRTOS tick requirement

Hybrid Arduino + ESP-IDF on Arduino-ESP32 3.3.11 requires
`CONFIG_FREERTOS_HZ=1000`. Do not change it to 100 Hz.

## Windows build-lock rule

`PermissionError [WinError 32]` under `managed_components` is an environment/file-lock
problem, not a source defect. Use `python tools/dev.py deepclean` or the PowerShell
cleanup helper before changing code.
