# PlatformIO / Codex setup — v0.3.2

```bash
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py build
```

Environments:
- home_display_p4_r3_dev — v3.x 400 MHz
- home_display_p4_r3_prod
- home_display_p4_legacy_dev — pre-v3 360 MHz, default for the connected v1.3 board
- home_display_p4_legacy_prod

Project-local board definitions keep pioarduino's correct chip variants while
declaring the actual 32-MB Waveshare flash.

CMake layers sdkconfig.defaults + revision overlay + dev/prod overlay.

Bootstrap:
- Windows: `powershell -ExecutionPolicy Bypass -File tools/bootstrap-platformio.ps1`
- Linux/macOS: `bash tools/bootstrap-platformio.sh`

VS Code tasks are committed under `.vscode/`.
