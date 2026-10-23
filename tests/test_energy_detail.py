"""Compile production aggregation and archive methods against a host SD/clock fixture."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class EnergyDetailTests(unittest.TestCase):
    def test_native_archive_and_aggregation(self):
        with tempfile.TemporaryDirectory(prefix="hp-detail-") as directory:
            folder=Path(directory)
            for file in ("EnergyDetailCore.h","EnergyDetailArchive.h","EnergyModel.h"):
                shutil.copyfile(ROOT/"src/services"/file,folder/file)
            for file in ("Arduino.h","FS.h"):(folder/file).write_text("#pragma once\n")
            source=(ROOT/"src/services/EnergyHistoryService.cpp").read_text()
            start=source.index("namespace {\nstruct DetailJournalHeader")
            (folder/"archive.inc").write_text(source[start:source.rfind("}")])
            shutil.copyfile(ROOT/"tests/native/energy_detail.cpp",folder/"check.cpp")
            exe=folder/("check.exe" if os.name=="nt" else "check")
            compiler=shutil.which("g++") or shutil.which("clang++")
            if compiler:cmd=[compiler,"-std=c++17","-I",str(folder),"check.cpp","-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists():self.skipTest("Host compiler missing")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" check.cpp /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],cwd=folder,capture_output=True,text=True,env={**os.environ,"TZ":"UTC"})
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_native_boundaries(self):
        source=(ROOT/"src/services/EnergyHistoryService.cpp").read_text()
        archive=source[source.index("namespace {\nstruct DetailJournalHeader"):]
        self.assertNotIn("writeAtomic",archive)
        self.assertIn("archiveVolume(volume)",archive)
        self.assertNotIn("fsFor(StorageClass::UserData)",archive)
        self.assertIn("1800000",archive)
        self.assertNotIn("xTaskCreate",archive)
        bridge=(ROOT/"src/apps/lua/LuaEnergyBridge.cpp").read_text()
        self.assertIn('"hp_energy_detail_history_status"',bridge)
        self.assertNotIn("setDetail",bridge)
