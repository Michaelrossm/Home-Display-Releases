import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class WaveshareBspTests(unittest.TestCase):
    def test_manifest(self):
        t=(ROOT/"src/idf_component.yml").read_text()
        self.assertIn("waveshare/esp32_p4_platform",t)
        self.assertIn('version: "2.0.1"',t)
        self.assertIn("lvgl/lvgl",t)
        self.assertIn('version: "9.3.0"',t)

    def test_component_registration_is_minimal(self):
        t=(ROOT/"src/CMakeLists.txt").read_text()
        active="\n".join(line for line in t.splitlines() if not line.lstrip().startswith("#"))
        self.assertNotIn("CONFIGURE_DEPENDS",active)
        self.assertNotIn("REQUIRES arduino",active)

    def test_no_duplicate_ui_libraries(self):
        t=(ROOT/"platformio.ini").read_text()
        self.assertNotIn("lvgl/lvgl@",t)
        self.assertNotIn("GFX Library for Arduino",t)

    def test_sd_pins(self):
        t=(ROOT/"include/BoardConfig.h").read_text()
        for s in ("kSdD0 = 39","kSdD1 = 40","kSdD2 = 41","kSdD3 = 42","kSdClk = 43","kSdCmd = 44"):
            self.assertIn(s,t)

    def test_display_audio_bsp_calls(self):
        d=(ROOT/"src/hardware/DisplayManager.cpp").read_text()
        a=(ROOT/"src/services/AudioHardwareService.cpp").read_text()
        self.assertIn("bsp_display_start_with_config",d)
        self.assertIn("bsp_audio_codec_speaker_init",a)

if __name__=="__main__": unittest.main()
