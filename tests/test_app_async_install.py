from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
def src(rel): return (ROOT / rel).read_text(encoding="utf-8")
class AppWorkerTests(unittest.TestCase):
    def test_http_does_not_call_registry_commit(self):
        h = src("src/services/FileTransferService.cpp")
        body = h.split("esp_err_t FileTransferService::appsLuaCommitHandler",1)[1].split("esp_err_t FileTransferService::appsLuaCancelHandler",1)[0]
        self.assertIn("commitFromHttp(",body)
        self.assertNotIn("AppRegistry::instance().rebuild()",body)
    def test_stage_validation_has_no_registry_access(self):
        t=src("src/apps/lua/LuaPackageInstaller.cpp")
        body=t.split("bool LuaPackageInstaller::validateStagedFiles",1)[1].split("bool LuaPackageInstaller::validateStage",1)[0]
        self.assertNotIn("AppManager::instance()",body)
        self.assertIn("validateLuaSyntax",body)
    def test_ui_owns_appmanager(self):
        t=src("src/apps/lua/LuaPackageInstaller.cpp")
        self.assertIn("processPendingUiCommit()",t)
        self.assertIn("registerValidatedApp(plan.manifest, live, !targetSd_&&!targetUsb_,targetUsb_,targetUsbGeneration_)",t)
        self.assertIn("beginPackageMutation(0)",t)
        self.assertIn("scanBusy()) return;",t)
        self.assertIn("refreshLauncherAfterInstall()",t)
        ui=src("src/ui/UiShell.cpp")
        self.assertIn("if (installPending) LuaPackageInstaller::instance().processPendingUiCommit();",ui)
        self.assertIn("!touchProbeDue && !installPending",ui)
    def test_native_postcall_budget(self):
        t=src("src/apps/lua/LuaApp.cpp")
        self.assertIn("millis() - startedMs",t)
        self.assertIn("Lua native call exceeded callback time budget",t)
        self.assertIn("lv_timer_pause(t)",t)
if __name__ == "__main__":unittest.main()
