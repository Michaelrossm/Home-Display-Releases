import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class LvglOwnershipTests(unittest.TestCase):
    def test_only_display_manager_services_lvgl(self):
        bad=[]
        for p in (ROOT/"src").rglob("*.cpp"):
            if p.name=="DisplayManager.cpp": continue
            if "lv_timer_handler()" in p.read_text(encoding="utf-8"):
                bad.append(str(p.relative_to(ROOT)))
        self.assertEqual(bad,[])

    def test_app_open_waits_for_bsp_render_lock(self):
        source=(ROOT/"src/ui/UiShell.cpp").read_text(encoding="utf-8")
        self.assertIn("uxQueueMessagesWaiting(pendingAppQueue_) > 0",source)
        self.assertIn("(installPending || appOpenPending) ? 1000 : 100",source)
        self.assertIn("xQueueOverwrite(shell.pendingAppQueue_, appId)",source)
        self.assertIn("xQueueReceive(pendingAppQueue_, appId, 0)",source)

    def test_cross_task_launcher_refresh_flag_is_atomic(self):
        header=(ROOT/"src/ui/UiShell.h").read_text(encoding="utf-8")
        source=(ROOT/"src/ui/UiShell.cpp").read_text(encoding="utf-8")
        self.assertIn("std::atomic<bool> appsRefreshPending_",header)
        self.assertIn("appsRefreshPending_.exchange(false",source)

    def test_settings_root_covers_transparent_launcher(self):
        source=(ROOT/"src/apps/settings/SettingsApp.cpp").read_text(encoding="utf-8")
        body=source.split("void SettingsApp::open(lv_obj_t* p)",1)[1].split(
            "void SettingsApp::rebuild()",1)[0]
        self.assertIn("UiTheme::instance().styleScreen(root_);",body)
        self.assertNotIn("LV_OPA_TRANSP",body)

    def test_app_host_is_opaque_and_home_restores_wallpaper(self):
        app_manager=(ROOT/"src/core/AppManager.cpp").read_text(encoding="utf-8")
        open_body=app_manager.split("bool AppManager::open(",1)[1].split(
            "bool AppManager::openSettings(",1)[0]
        settings_body=app_manager.split("bool AppManager::openSettings(",1)[1].split(
            "void AppManager::closeCurrent()",1)[0]
        shell=(ROOT/"src/ui/UiShell.cpp").read_text(encoding="utf-8")
        home_body=shell.split("void UiShell::rebuildHome()",1)[1].split(
            "void UiShell::homePageBtn",1)[0]
        self.assertIn("UiTheme::instance().styleScreen(parent);",open_body)
        self.assertIn("UiTheme::instance().styleScreen(parent);",settings_body)
        self.assertIn("lv_obj_set_style_bg_opa(home_,LV_OPA_TRANSP,0);",home_body)

    def test_app_transition_removes_home_centrally_and_invalidates_once(self):
        source=(ROOT/"src/core/AppManager.cpp").read_text(encoding="utf-8")
        opening=source.split("bool AppManager::open(",1)[1].split("bool AppManager::openSettings(",1)[0]
        for token in ("suspendCurrent();", "hideHomeBackground(parent)",
                      "styleScreen(parent)", "lv_obj_clean(parent)",
                      "current_->open(parent)", "lv_obj_invalidate(parent)"):
            self.assertIn(token, opening)
        self.assertLess(opening.index("suspendCurrent();"), opening.index("lv_obj_clean(parent)"))
        self.assertLess(opening.index("lv_obj_clean(parent)"), opening.index("current_->open(parent)"))
        self.assertEqual(opening.count("lv_obj_invalidate(parent)"), 1)

    def test_home_is_not_rebuilt_or_wallpaper_refreshed_inside_app(self):
        shell=(ROOT/"src/ui/UiShell.cpp").read_text(encoding="utf-8")
        home=shell.split("void UiShell::rebuildHome()",1)[1].split("void UiShell::homePageBtn",1)[0]
        guard=home.index("if (AppManager::instance().current()) return;")
        self.assertLess(guard, home.index("WallpaperService::instance().refresh()"))
        self.assertLess(guard, home.index("lv_obj_clean(home_)"))
        self.assertIn("setHiddenIfChanged(wallpaperLayer_, false);", home)
        hide=shell.split("void UiShell::hideHomeBackground",1)[1].split("void UiShell::appsChanged",1)[0]
        self.assertIn("if (host != content_) return;", hide)
        self.assertIn("setHiddenIfChanged(wallpaperLayer_, true);", hide)
        refresh=shell.split("void UiShell::refreshTheme()",1)[1].split("static void setLabelIfChanged",1)[0]
        self.assertNotIn("WallpaperService::instance().refresh()", refresh)
        self.assertIn("if (!AppManager::instance().current()) rebuildHome();", refresh)

    def test_screen_background_has_no_transparent_rounded_corners(self):
        source=(ROOT/"src/ui/UiTheme.cpp").read_text(encoding="utf-8")
        screen=source.split("void UiTheme::styleScreen",1)[1].split("void UiTheme::styleTopBar",1)[0]
        self.assertIn("lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);", screen)
        self.assertIn("lv_obj_set_style_radius(obj, 0, 0);", screen)

    def test_same_app_settings_stops_old_timers_before_deleting_objects(self):
        source=(ROOT/"src/core/AppManager.cpp").read_text(encoding="utf-8")
        settings=source.split("bool AppManager::openSettings(",1)[1].split("void AppManager::closeCurrent()",1)[0]
        self.assertNotIn("current_ != app", settings)
        self.assertLess(settings.index("suspendCurrent();"), settings.index("lv_obj_clean(parent)"))
        self.assertLess(settings.index("app->close();"), settings.index("lv_obj_clean(parent)"))

    def test_settings_close_stops_every_owned_ui_timer(self):
        header=(ROOT/"src/apps/settings/SettingsApp.h").read_text(encoding="utf-8")
        close=header.split("void close()override{",1)[1].split("private:",1)[0]
        for timer in ("scanTimer_", "bleTimer_", "nasTimer_", "scrollSettleTimer_"):
            self.assertIn(f"lv_timer_delete({timer})", close)
            self.assertIn(f"{timer}=nullptr", close)
        self.assertIn("deferredWifiRefresh_=deferredBleRefresh_=deferredNasRefresh_=false", close)

    def test_app_trace_does_not_flush_persistent_storage_under_ui_lock(self):
        manager=(ROOT/"src/core/AppManager.cpp").read_text(encoding="utf-8")
        for code in ("APP_OPEN_REQ", "APP_REGISTRY_FOUND", "APP_VIEW_READY", "APP_SETTINGS_OPEN"):
            self.assertIn(f'addTransient("INFO", "{code}"', manager)
        self.assertIn('add("ERROR", "APP_VIEW_EMPTY"', manager)
        self.assertIn('add("WARN", "APP_DISABLED"', manager)
        logger=(ROOT/"src/core/EventLog.cpp").read_text(encoding="utf-8")
        self.assertIn("return record(level,code,m,true)", logger)
        self.assertIn("return record(level,code,m,false)", logger)
        self.assertIn("appendRam(r);if(!durable||!persistent_)return true", logger)
        self.assertIn("xQueueSend(pending_,&r,0)",logger)
        self.assertIn("void EventLog::backgroundTick()",logger)

if __name__=="__main__": unittest.main()
