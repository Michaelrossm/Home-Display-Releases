# Home-Display: deterministic, read-only pre-build validation.
# Old versions restored deleted files from Git and patched C++ at build time.
# This script deliberately does not modify the source tree.
Import("env")
from pathlib import Path
project = Path(env["PROJECT_DIR"])
required = [
    "src/apps/radio/RadioService.h",
    "src/apps/radio/RadioService.cpp",
    "src/apps/music/MusicPlayerService.h",
    "src/apps/music/MusicPlayerService.cpp",
    "src/apps/weather/WeatherService.h",
    "src/apps/weather/WeatherService.cpp",
    "src/apps/power/PowerService.h",
    "src/apps/power/PowerService.cpp",
    "src/apps/calendar/CalendarService.h",
    "src/apps/calendar/CalendarService.cpp",
    "src/apps/goliath/GoliathService.h",
    "src/apps/goliath/GoliathService.cpp",
]
missing = [rel for rel in required if not (project / rel).is_file()]
if missing:
    raise RuntimeError("Required shared backends missing: " + ", ".join(missing))
ui = (project / "src/ui/UiShell.cpp").read_text(encoding="utf-8")
if "alwaysOnEnabled" not in ui:
    raise RuntimeError("UiShell AOD implementation missing; do not patch source during build")
print("[Home-Display] checked: backends and AOD present; sources unchanged")
