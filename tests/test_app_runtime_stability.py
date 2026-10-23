from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

class AppRuntimeRegression(unittest.TestCase):
    def test_failed_callbacks_quarantine_without_screen_reload(self):
        s = source("src/apps/lua/LuaApp.cpp")
        body=s.split("void LuaApp::quarantine(",1)[1].split("bool LuaApp::suspendTo",1)[0]
        for expected in ("faulted_=true", "paused_=true", "lv_timer_pause(timer_)",
                         "stopUiAnimations(root_)", "cancelOwner(id())"):
            self.assertIn(expected,body)
        for forbidden in ("lv_obj_delete", "lv_obj_clean", "lua_close", "lv_screen_load", "AppManager::"):
            self.assertNotIn(forbidden,body)
        for expected in ('quarantine("on_event")', 'quarantine("on_tick")', 'quarantine(name.c_str())'):
            self.assertIn(expected,s)
        self.assertIn('if(faulted_)return;',s.split("void LuaApp::resume()",1)[1].split("namespace {",1)[0])

    def test_lua_tick_failure_is_paused(self):
        s = source("src/apps/lua/LuaApp.cpp")
        self.assertIn("lv_timer_pause(t)", s)
        self.assertIn('hasGlobal("on_tick")', s)
        self.assertIn("kTickHardBudgetMs", s)
        self.assertIn("Lua native call exceeded callback time budget", s)

    def test_lua_view_is_released(self):
        s = source("src/apps/lua/LuaApp.cpp")
        self.assertIn("lv_obj_delete(root_)", s)
        self.assertIn("ownedNames_.clear()", s)

    def test_scan_is_serialized_with_installs(self):
        reg = source("src/apps/lua/LuaAppRegistry.cpp")
        installer = source("src/apps/lua/LuaPackageInstaller.cpp")
        self.assertIn("xSemaphoreTake(packageIoMutex_, portMAX_DELAY)", reg)
        self.assertIn("xSemaphoreGive(packageIoMutex_)", reg)
        self.assertIn("beginPackageMutation(0)", installer)

    def test_http_does_not_directly_free_lua_app(self):
        s = source("src/services/FileTransferService.cpp")
        self.assertIn("LuaPackageService::instance().remove(id,", s)
        service=source("src/services/LuaPackageService.cpp")
        self.assertIn("registry.queueUnregister(id.c_str())", service)
        self.assertNotIn("unregisterApp(id.c_str())", service)
        self.assertNotIn("LuaAppRegistry::instance().unregisterApp(id.c_str())", s)

    def test_ui_applies_queued_removal(self):
        s = source("src/apps/lua/LuaAppRegistry.cpp")
        self.assertIn("pendingUnregisterCount_", s)
        self.assertIn("unregisterApp(removed[i].c_str())", s)

if __name__ == "__main__":
    unittest.main()
