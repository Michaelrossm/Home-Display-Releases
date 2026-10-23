"""Read-only firmware regression checks, based on the current P4 project.

These assertions inspect the production code; the optional PlatformIO build in
run_checks.py catches actual C++ compilation and link failures. They are not
hardware/GUI/network runtime tests.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text, name):
    """Find a C++ method body by its qualified name, preserving nested braces."""
    match = re.search(r"^[ \t]*(?:void|bool|int|String|esp_err_t|uint8_t|uint32_t)\s+" + re.escape(name) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{", text, re.M)
    if not match:
        raise AssertionError(f"Function not found: {name}")
    start = match.end()
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
    raise AssertionError(f"Unclosed function: {name}")


class FirmwareRegressionTests(unittest.TestCase):
    def test_settings_callbacks_have_implementations(self):
        header = source("src/apps/settings/SettingsApp.h")
        impl = source("src/apps/settings/SettingsApp.cpp")
        callbacks = set(re.findall(r"static\s+void\s+(\w+)\s*\(\s*lv_event_t\s*\*\s*\)\s*;", header))
        self.assertGreaterEqual(len(callbacks), 10, "Unexpectedly few settings callbacks")
        for callback in callbacks:
            with self.subTest(callback=callback):
                function_body(impl, f"SettingsApp::{callback}")
        self.assertIn("wifiProfileCb", callbacks, "Previous missing wifiProfileCb declaration regressed")

    def test_independent_wifi_switches(self):
        app = source("src/apps/settings/SettingsApp.cpp")
        body = function_body(app, "SettingsApp::wifiEcoChanged")
        self.assertIn("changed == self->wifiOffOnLanSwitch_", body)
        self.assertIn("changed == self->wifiPowerSaveSwitch_", body)
        self.assertIn("setWifiOffWhenLan(enabled)", body)
        self.assertIn("setWifiPowerSave(enabled)", body)
        self.assertNotRegex(body, r"!self->wifiPowerSaveSwitch_\s*\|\|\s*!self->wifiOffOnLanSwitch_",
                            "Each setting must be editable on its own page")
        settings = source("src/services/SettingsService.cpp")
        for method, key in (("setWifiPowerSave", "net_wps"), ("setWifiOffWhenLan", "net_wlanoff")):
            with self.subTest(method=method):
                method_body = function_body(settings, f"SettingsService::{method}")
                self.assertIn(key, method_body)
                self.assertIn("putBool", method_body)
                self.assertNotIn("save(s)", method_body)

    def test_four_stages_and_aod_have_persisted_fields(self):
        header = source("src/services/SettingsService.h")
        settings = source("src/services/SettingsService.cpp")
        idle = source("src/services/IdleService.cpp")
        shell = function_body(source("src/ui/UiShell.cpp"), "UiShell::applyIdle")
        for symbol in ("dimAfter", "saverAfter", "offAfter", "alwaysOnEnabled", "alwaysOnBrightness"):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, header)
                self.assertIn(symbol, settings)
        for stage in ("IdleStage::Dimmed", "IdleStage::Saver", "IdleStage::Off"):
            self.assertIn(stage, idle)
        self.assertRegex(shell, r"alwaysOnEnabled\s*\?\s*p\.alwaysOnBrightness\s*:\s*0")
        self.assertIn("setHiddenIfChanged(lv_obj_get_child(saver_,2), true)", shell)
        helper = source("src/ui/UiShell.cpp")
        self.assertIn("lv_obj_add_flag", helper)  # cached helper still hides controls
        self.assertIn("TimeService::instance().timeText()", shell)
        self.assertIn("TimeService::instance().dateText()", shell)

    def test_wifi_schedule_handles_midnight_and_requires_valid_time(self):
        code = source("src/services/NetworkService.cpp")
        body = function_body(code, "NetworkService::shouldScheduleWifiOff")
        self.assertIn("TimeService::instance().valid()", body)
        self.assertIn("IdleService::instance().stage() != IdleStage::Off", body)
        self.assertRegex(body, r"start\s*==\s*end")
        self.assertRegex(body, r"start\s*<\s*end")
        self.assertRegex(body, r"minute\s*>=\s*start\s*\|\|\s*minute\s*<\s*end")
        loop = function_body(code, "NetworkService::loop")
        self.assertIn("shouldScheduleWifiOff(", loop)

    def test_wifi_details_are_throttled_and_eco_changes_deduplicated(self):
        net = source("src/services/NetworkService.cpp")
        self.assertIn("lastWifiDetails_", net)
        self.assertIn("5000UL", net)
        self.assertRegex(net, r"appliedWifiPs_\s*!=\s*nextPs")
        self.assertIn("wifiScanInProgress()", net)

    def test_display_mode_and_no_redundant_backlight_writes(self):
        display = source("src/hardware/DisplayManager.cpp")
        self.assertRegex(display, r"\.tear_avoid_mode\s*=\s*ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL")
        self.assertIn('" tear=triple_partial"', display)
        brightness = function_body(display, "DisplayManager::setBrightness")
        self.assertIn("brightness_", brightness)
        self.assertIn("bsp_display_brightness_set", brightness)
        shell = function_body(source("src/ui/UiShell.cpp"), "UiShell::applyIdle")
        self.assertRegex(shell, r"DisplayManager::instance\(\)\.brightness\(\)\s*!=\s*target")

    def test_fonts_and_common_widgets_exist(self):
        theme = source("src/ui/UiTheme.cpp")
        header = source("src/ui/UiFonts.h")
        widgets = source("src/ui/SettingsWidgets.h")
        for size in (14, 18, 24, 28, 32, 48):
            with self.subTest(size=size):
                self.assertIn(f"hp_font_de_{size}", theme)
                self.assertIn(f"hp_font_de_{size}", header)
                self.assertTrue((ROOT / f"src/ui/fonts/UiFontDe{size}.c").is_file())
        self.assertIn("textPrimary", widgets)
        self.assertIn("fontWithGermanFallback", theme)

    def test_build_hook_does_not_rewrite_firmware_sources(self):
        hook = source("tools/prebuild_app_cleanup.py")
        for write_action in ("write_text(", "write_bytes(", "unlink(", "git restore", "git show"):
            with self.subTest(action=write_action):
                self.assertNotIn(write_action, hook)

    def test_every_web_handler_checks_auth_before_processing(self):
        web = source("src/services/FileTransferService.cpp")
        names = re.findall(r"esp_err_t\s+FileTransferService::(\w+Handler)\s*\(\s*httpd_req_t\s*\*\s*req\s*\)\s*\{", web)
        self.assertGreaterEqual(len(names), 14, "Unexpectedly few HTTP handlers")
        for name in names:
            with self.subTest(handler=name):
                body = function_body(web, f"FileTransferService::{name}")
                self.assertRegex(body.lstrip()[:180], r"^if\s*\(\s*!(?:webAdminAuthorized|writeAuthorized)\(req\)\s*\)\s*return\s+ESP_OK\s*;")

        write_guard=function_body(web,"writeAuthorized")
        self.assertIn("webAdminAuthorized(req) && WebCsrfService::instance().validate(req)",write_guard)
        guard = function_body(web, "webAdminAuthorized")
        self.assertIn("WebAdminAuth::authorized(req)", guard)
        self.assertIn('"Authorization"', source("src/services/WebAdminAuth.cpp"))
        self.assertIn("Sec-Fetch-Site", guard)
        self.assertIn("WebLoginService::deny(req)", guard)
        self.assertNotIn("WWW-Authenticate", guard)

    def test_compilation_profiles_match_revision(self):
        pio = ROOT / "platformio.ini"
        if not pio.exists():
            self.skipTest("Uploaded source archive does not contain platformio.ini; full-build runner checks it")
        config = pio.read_text(encoding="utf-8")
        import configparser
        parsed = configparser.ConfigParser(interpolation=None)
        parsed.read_string(config)
        for revision in ("r3", "legacy"):
            for mode in ("dev", "prod"):
                self.assertEqual(parsed[f"env:home_display_p4_{revision}_{mode}"]["board"],
                                 f"waveshare_home_display_p4_{revision}")


if __name__ == "__main__":
    unittest.main()
