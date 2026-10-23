import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class HybridSdkconfigTests(unittest.TestCase):
    def test_freertos_tick_required_by_arduino(self):
        text=(ROOT/"sdkconfig.defaults").read_text()
        self.assertIn("CONFIG_FREERTOS_HZ=1000", text)

    def test_no_unknown_p4_spiram_symbol(self):
        for rel in ("sdkconfig.defaults","config/sdkconfig.rev3_x",
                    "config/sdkconfig.rev1_3","config/sdkconfig.dev",
                    "config/sdkconfig.prod"):
            p=ROOT/rel
            if p.exists():
                self.assertNotIn("CONFIG_ESP32P4_SPIRAM_SUPPORT", p.read_text())

    def test_bsp_i2c_speed_not_forced_while_fast_mode_selects_it(self):
        text=(ROOT/"sdkconfig.defaults").read_text()
        self.assertIn("CONFIG_BSP_I2C_FAST_MODE=y", text)
        self.assertNotIn("CONFIG_BSP_I2C_CLK_SPEED_HZ=", text)

if __name__=="__main__":
    unittest.main()
