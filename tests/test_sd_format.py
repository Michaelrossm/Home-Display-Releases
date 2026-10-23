import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class SdFormatTests(unittest.TestCase):
    def test_confirmation_and_boot_format(self):
        with tempfile.TemporaryDirectory(prefix="hp-sd-format-") as directory:
            folder=Path(directory)
            shutil.copyfile(ROOT/"src/services/SdFormatConfirmation.h",folder/"SdFormatConfirmation.h")
            shutil.copyfile(ROOT/"tests/native/sd_format.cpp",folder/"check.cpp")
            source=(ROOT/"src/services/StorageService.cpp").read_text()
            start=source.index("StorageService& StorageService::instance()")
            singleton=source[start:source.index("\n",start)+1]
            format_start=source.rfind("namespace {",0,source.index("bool sdFormatTarget("))
            (folder/"format.inc").write_text(singleton+source[format_start:source.index("fs::FS* StorageService::sdUserDataFs()")])
            exe=folder/("check.exe" if os.name=="nt" else "check");compiler=shutil.which("g++") or shutil.which("clang++")
            if compiler:cmd=[compiler,"-std=c++17","-I",str(folder),"check.cpp","-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists():self.skipTest("Host compiler missing")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip();vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" check.cpp /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_ui_and_startup_order(self):
        ui=(ROOT/"src/apps/settings/SettingsApp.cpp").read_text()
        self.assertIn("Bestaetigung 1 von 2",ui);self.assertIn("Bestaetigung 2 von 2",ui);self.assertIn("sdFormatCancelCb",ui)
        self.assertNotIn("f_mkfs",ui);self.assertNotIn("writeRAW",ui)
        main=(ROOT/"src/main.cpp").read_text()
        self.assertLess(main.index("ConfigService::instance().begin()"),main.index("BootManager::instance().begin()"))
        self.assertLess(main.index("applyBootSdFormat"),main.index("beginSharedAppServices"))
        code=(ROOT/"src/services/StorageService.cpp").read_text()
        boot=code.split("void StorageService::applyBootSdFormat",1)[1].split("fs::FS*",1)[0]
        self.assertLess(boot.index('remove("sd_fmt_req")'),boot.index("f_mkfs"));self.assertIn('config.getU32("boot_count")',boot)
