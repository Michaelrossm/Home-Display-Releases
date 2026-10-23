"""Production manager/lifecycle/scheduler methods, with simulated LVGL and services."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class AppLifecycleTests(unittest.TestCase):
    def test_native_manager_and_scheduler(self):
        with tempfile.TemporaryDirectory(prefix="hp-lifecycle-") as directory:
            folder=Path(directory)
            for name in ("AppManager.h","AppPermissions.h","AppLifecycleCore.h","AppBackgroundScheduler.h"):
                shutil.copyfile(ROOT/"src/core"/name,folder/name)
            for name in ("Arduino.h","lvgl.h"):(folder/name).write_text("#pragma once\n")
            (folder/"freertos").mkdir();(folder/"freertos/semphr.h").write_text("#pragma once\n")
            (folder/"BackgroundScheduler.h").write_text('''#pragma once
namespace hp {enum class BackgroundJobClass{Blocking};class IBackgroundJob{public:virtual ~IBackgroundJob()=default;virtual void backgroundTick()=0;virtual BackgroundJobClass backgroundClass()const=0;virtual uint32_t backgroundIntervalMs()const=0;virtual const char* backgroundName()const=0;};class BackgroundScheduler{public:static BackgroundScheduler& instance(){static BackgroundScheduler s;return s;}bool add(IBackgroundJob*){return true;}};}
''')
            source=(ROOT/"src/core/AppManager.cpp").read_text();(folder/"manager.inc").write_text(source[source.index("namespace hp {"):])
            source=(ROOT/"src/core/BackgroundScheduler.cpp").read_text();(folder/"background.inc").write_text(source[source.index("AppBackgroundScheduler& AppBackgroundScheduler::instance"):source.index("BackgroundScheduler& BackgroundScheduler::instance")])
            source=(ROOT/"src/apps/lua/LuaApp.cpp").read_text();(folder/"lifecycle.inc").write_text(source[source.index("void LuaApp::pause()"):source.index("bool LuaApp::hasSettings()")])
            source=(ROOT/"src/apps/lua/LuaAppManifest.cpp").read_text();start=source.index('    if(!doc["background"].isUnbound())');end=source.index('    if (out.schema == 3)',start)
            (folder/"manifest.inc").write_text(source[start:end])
            shutil.copyfile(ROOT/"tests/native/app_lifecycle.cpp",folder/"check.cpp")
            exe=folder/("check.exe" if os.name=="nt" else "check");compiler=shutil.which("g++") or shutil.which("clang++")
            json_dir=ROOT/".pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src"
            if compiler:cmd=[compiler,"-std=c++17","-I",str(folder),"-I",str(json_dir),"check.cpp","-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists():self.skipTest("Host compiler missing")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip();vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" /I"{json_dir}" check.cpp /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_guarded_lua_background_execution_without_tasks(self):
        bridge=(ROOT/"src/apps/lua/LuaNavigationBridge.cpp").read_text()
        self.assertIn('"hp_background_capabilities"',bridge);self.assertIn('"hp_background_request"',bridge)
        self.assertIn('lua_pushboolean(L,true);lua_setfield(L,-2,"lua_callback")',bridge)
        self.assertIn('runLuaBackground(L_,micros', (ROOT/"src/apps/lua/LuaApp.cpp").read_text())
        manager=(ROOT/"src/core/AppManager.cpp").read_text();self.assertIn("suspended_.remove(app)",manager);self.assertIn("resumeTo(parent)",manager)
        self.assertNotIn("xTaskCreate",manager)
        runtime=(ROOT/"src/apps/lua/LuaApp.cpp").read_text();self.assertIn("app->paused_",runtime);self.assertIn("binding->app->paused_",runtime)
        self.assertIn('callGlobal("on_suspend",10,50)',runtime);self.assertIn('callGlobal("on_evict",10,50)',runtime)
        parser=(ROOT/"src/apps/lua/LuaAppManifest.cpp").read_text();self.assertIn("manifest_background_unknown",parser);self.assertIn("appPolicy(p)",parser)
