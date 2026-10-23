#!/usr/bin/env python3
"""Read-only structural checks. Run: python tools/verify_project.py"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
errors = []

def read(name):
    path = root / name
    if not path.is_file():
        errors.append(f"Missing file: {name}")
        return ""
    return path.read_text(encoding="utf-8")

net_h = read("src/services/NetworkService.h")
net_cpp = read("src/services/NetworkService.cpp")
ui = read("src/ui/UiShell.cpp")
settings = read("src/apps/settings/SettingsApp.cpp")
settings_h = read("src/apps/settings/SettingsApp.h")
web = read("src/services/FileTransferService.cpp")
prebuild = read("tools/prebuild_app_cleanup.py")

for decl in re.findall(r"static void (\w+)\(lv_event_t\*\);", settings_h):
    if f"SettingsApp::{decl}(" not in settings:
        errors.append(f"Settings callback missing implementation: {decl}")
for name in ("updateConnectionState", "applyWifiEcoPolicy", "shouldScheduleWifiOff"):
    if f"NetworkService::{name}(" not in net_cpp:
        errors.append(f"Missing NetworkService implementation: {name}")
for key in ("wifiOffWhenLan", "wifiScheduleEnabled", "alwaysOnEnabled"):
    if key not in (net_h + net_cpp + ui + settings + read("src/services/SettingsService.h")):
        errors.append(f"Missing feature: {key}")
if "webAdminAuthorized(req)" not in web or "WWW-Authenticate" not in web:
    errors.append("Web admin authentication is missing")
if "write_text(" in prebuild or "write_bytes(" in prebuild or "git restore" in prebuild:
    errors.append("Build hook must not modify the source tree")
if "TRIPLE_PARTIAL" not in read("src/hardware/DisplayManager.cpp"):
    errors.append("Expected rotated partial display buffer mode is missing")
if errors:
    print("FAIL: " + "\nFAIL: ".join(errors))
    sys.exit(1)
print("PASS: backend/UI declarations, power/WLAN, authentication and read-only build hook")
