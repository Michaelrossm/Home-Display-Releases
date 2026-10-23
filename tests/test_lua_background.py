from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class LuaBackgroundTests(unittest.TestCase):
    def test_real_vm_and_production_lifecycle_method(self):
        cmake=shutil.which('cmake') or str(Path.home()/'.platformio/packages/tool-cmake/bin/cmake.exe')
        ninja=shutil.which('ninja') or str(Path.home()/'.platformio/packages/tool-ninja/ninja.exe')
        if not Path(cmake).exists() or not Path(ninja).exists(): self.skipTest('Native CMake/Ninja unavailable')
        # Parallel validator/test runs must not write the same MSVC object/PDB.
        (ROOT/'.pio').mkdir(parents=True,exist_ok=True)
        temporary=tempfile.TemporaryDirectory(prefix='native_lua_background_',dir=ROOT/'.pio')
        self.addCleanup(temporary.cleanup)
        build=Path(temporary.name)
        text=(ROOT/'src/apps/lua/LuaApp.cpp').read_text(encoding='utf-8')
        method=text[text.index('bool LuaApp::runBackground('):text.index('bool LuaApp::dispatchBackground(')]
        self.assertNotIn('lv_',method);self.assertNotIn('resume(',method);self.assertNotIn('quarantine(',method)
        (build/'background_method.inc').write_text(method,encoding='utf-8')
        routing=text[text.index('    IApp* current=AppManager::instance().current();'):text.index('bool LuaApp::runBackground(')]
        (build/'event_dispatch.inc').write_text('bool LuaApp::dispatchEvents(bool allowBackground){\n'+routing,encoding='utf-8')
        source=ROOT/'tests/native/lua_background'
        commands=f'"{cmake}" -S "{source}" -B "{build}" -G Ninja -DCMAKE_MAKE_PROGRAM="{ninja}" && "{cmake}" --build "{build}" --parallel 4'
        if os.name=='nt':
            setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
            if not setup.exists(): self.skipTest('Host compiler unavailable')
            command=f'cmd /d /c "call "{setup}" >nul && {commands}"'
        else: command=['sh','-c',commands]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        self.assertEqual(result.returncode,0,(result.stdout+result.stderr)[-5000:])
        exe=build/('background_check.exe' if os.name=='nt' else 'background_check')
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_owner_completion_and_ui_handoff(self):
        scheduler=(ROOT/'src/core/BackgroundScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('if(!pending_.handler)',scheduler)
        runtime=(ROOT/'src/apps/lua/LuaApp.cpp').read_text(encoding='utf-8')
        self.assertIn('xTaskGetCurrentTaskHandle()!=backgroundUiTask_',runtime)
        self.assertIn('parked->runBackground(event.name,event.job)',runtime)
        shell=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        self.assertLess(shell.index('DisplayUiGuard uiGuard(uiLockTimeoutMs)'),shell.index('LuaApp::dispatchBackground()'))
        for path in ('src/apps/lua/LuaBackgroundRun.cpp','src/apps/lua/LuaApp.cpp'):
            self.assertNotIn('xTaskCreate',(ROOT/path).read_text(encoding='utf-8'))
        jobs=(ROOT/'src/apps/lua/LuaJobService.cpp').read_text(encoding='utf-8')
        self.assertIn('discardLuaEvents(owner)',jobs);self.assertIn('slot.request.owner==owner',jobs)
