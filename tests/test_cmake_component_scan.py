import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class CMakeComponentScanTests(unittest.TestCase):
    def test_script_mode_safe(self):
        text=(ROOT/"src/CMakeLists.txt").read_text(encoding="utf-8")
        active="\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        self.assertIn("file(GLOB_RECURSE HOMEDISPLAY_SOURCES",active)
        self.assertNotIn("CONFIGURE_DEPENDS",active)
        self.assertNotIn("REQUIRES arduino",active)

if __name__=="__main__": unittest.main()
