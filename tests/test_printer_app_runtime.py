from pathlib import Path
import subprocess
import unittest
from test_printer_package import syntax

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'apps/printer_panel_professional'

@unittest.skipUnless(APP.is_dir(), 'Installable printer package is local, not shipped with firmware')
class PrinterAppRuntimeTests(unittest.TestCase):
    def test_package_syntax_and_real_lua_runtime(self):
        with syntax.LuaSyntaxChecker() as checker:
            for path in APP.glob('*.lua'):
                checker.check(path.read_bytes(),path.name)
        with syntax.LuaSyntaxChecker('lua_runtime_check.c') as runner:
            result=subprocess.run([str(runner.exe),str(ROOT/'tests/native/printer_app_runtime.lua'),str(APP/'main.lua')],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
