import json, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class TestDeveloperSetup(unittest.TestCase):
    def test_boards(self):
        r=json.loads((ROOT/"boards/waveshare_home_display_p4_r3.json").read_text())
        l=json.loads((ROOT/"boards/waveshare_home_display_p4_legacy.json").read_text())
        self.assertEqual(r["upload"]["flash_size"],"32MB")
        self.assertEqual(l["upload"]["flash_size"],"32MB")
        self.assertEqual(r["build"]["chip_variant"],"esp32p4")
        self.assertEqual(l["build"]["chip_variant"],"esp32p4_es")
        self.assertEqual(r["build"]["f_cpu"],"400000000L")
        self.assertEqual(l["build"]["f_cpu"],"360000000L")
    def test_revision_overlays(self):
        r=(ROOT/"config/sdkconfig.rev3_x").read_text()
        l=(ROOT/"config/sdkconfig.rev1_3").read_text()
        self.assertIn("CONFIG_ESP32P4_REV_MIN_300=y",r)
        self.assertIn("CONFIG_ESP32P4_REV_MIN_100=y",l)
    def test_handoff(self):
        for f in ("AGENTS.md","CODEX_HANDOFF.md","tools/dev.py","CMakeLists.txt","src/CMakeLists.txt"):
            self.assertTrue((ROOT/f).is_file())
    def test_platformio(self):
        p=(ROOT/"platformio.ini").read_text()
        self.assertIn("boards_dir = boards",p)
        self.assertIn("waveshare_home_display_p4_r3",p)
        self.assertIn("waveshare_home_display_p4_legacy",p)
if __name__=="__main__": unittest.main()
