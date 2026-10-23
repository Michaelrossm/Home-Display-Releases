"""Run with: python -m unittest discover -s tests"""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
def text(path): return (ROOT/path).read_text(encoding='utf-8')

class ProjectInvariants(unittest.TestCase):
    def test_build_hook_is_read_only(self):
        hook=text("tools/prebuild_app_cleanup.py")
        for forbidden in ("git restore", "write_text(", "write_bytes(", "update_ui_text("):
            self.assertNotIn(forbidden, hook)
    def test_network_queries_throttled(self):
        net=text("src/services/NetworkService.cpp")
        self.assertIn("lastWifiDetails_",net)
        self.assertIn("5000UL",net)
        policy=net.split("void NetworkService::applyWifiEcoPolicy()",1)[1].split("void NetworkService::updateAdaptiveTxPower()",1)[0]
        self.assertIn("if (appliedWifiPs_ != nextPs)",policy)
        self.assertIn("if(WiFi.setSleep(ps))",policy)
        self.assertGreater(policy.index("WiFi.getMode()"),policy.index("if (appliedWifiPs_ != nextPs)"))
    def test_aod_and_wifi_preserved(self):
        self.assertIn("alwaysOnEnabled",text("src/ui/UiShell.cpp"))
        s=text("src/apps/settings/SettingsApp.cpp")
        self.assertIn("wifiScheduleChanged",s)
        self.assertIn("wifiOffOnLanSwitch_",s)
    def test_admin_auth_all_http_handlers(self):
        import re
        web=text("src/services/FileTransferService.cpp")
        handlers=re.findall(r"esp_err_t FileTransferService::\w+Handler\(httpd_req_t\* req\) \{([^{}]*)",web)
        self.assertGreaterEqual(len(handlers),14)
        for body in handlers:
            self.assertRegex(body,r"!(?:webAdminAuthorized|writeAuthorized)\(req\)")
        self.assertIn("return webAdminAuthorized(req) && WebCsrfService::instance().validate(req);",web)
        self.assertIn("Sec-Fetch-Site",web)
        self.assertIn("WebLoginService::deny(req)",web)

if __name__ == "__main__": unittest.main()
