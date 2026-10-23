import unittest
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class BuildHygieneTests(unittest.TestCase):
    def test_retired_patch_installers_and_native_shells_stay_removed(self):
        for rel in ('installieren.py','ERZEUGEN.py','INTEGRATION.patch','OCTOPRINT-INTEGRATION.patch',
                    'tools/apply_usb_update.py','tools/restore-shared-app-backends.ps1',
                    'src/apps/strom/StromApp.cpp','src/apps/diagnostics/DiagnosticsApp.cpp','src/apps/diagnostics/RenderTestApp.cpp',
                    'src/apps/home/HomeApp.h','tools/package_calendar.py','tools/package_fitz_touch.py',
                    'docs/calendar-local-integration.md','tests/native/calculator_app_runtime.lua','tests/native/fitz_touch.lua'):
            self.assertFalse((ROOT/rel).exists(),rel)

    def test_current_documentation_links_and_partition_metadata(self):
        import json,re
        from urllib.parse import urlsplit, unquote
        manifest=json.loads((ROOT/'PROJECT_MANIFEST.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['partition_layout']['ota_0'],'9 MiB')
        self.assertEqual(manifest['developer_setup']['default_env'],'home_display_p4_legacy_dev')
        for rel in ('README.md','docs/README.md'):
            path=ROOT/rel
            for target in re.findall(r'\]\(([^)]+)\)',path.read_text(encoding='utf-8')):
                url=urlsplit(target)
                if url.scheme in ('http','https'):
                    self.assertTrue(url.hostname, target)
                    self.assertIsNone(url.username, target)
                    continue
                self.assertFalse(url.scheme, target)
                self.assertTrue((path.parent/unquote(url.path)).is_file(),target)

    def test_deepclean_exists(self):
        text=(ROOT/"tools/dev.py").read_text()
        self.assertIn("def deepclean()", text)
        self.assertIn('sp.add_parser("deepclean")', text)

    def test_windows_cleanup_script_exists(self):
        self.assertTrue((ROOT/"tools/clean_windows_component_locks.ps1").is_file())

    def test_no_generated_state_committed(self):
        for rel in (".pio","managed_components"):
            tracked = subprocess.run(["git", "ls-files", "--", rel], cwd=ROOT,
                                     capture_output=True, text=True, check=True)
            self.assertFalse(tracked.stdout.strip(), rel)
        self.assertTrue((ROOT/'dependencies.lock').is_file(), 'Pinned component resolution must be retained')

    def test_cmake_still_minimal(self):
        text=(ROOT/"src/CMakeLists.txt").read_text()
        active="\n".join(x for x in text.splitlines() if not x.lstrip().startswith("#"))
        self.assertNotIn("CONFIGURE_DEPENDS", active)
        self.assertNotIn("REQUIRES arduino", active)

if __name__=="__main__":
    unittest.main()
