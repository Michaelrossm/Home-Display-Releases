"""Regression guards for UI polling; device responsiveness needs hardware QA."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class UiPollEfficiency(unittest.TestCase):
    def source(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_idle_and_drag_rates(self):
        text = self.source("src/ui/UiShell.cpp")
        for stage, period in (("Dimmed", 50), ("Saver", 100), ("Off", 200)):
            self.assertIn(f"IdleStage::{stage})uiPeriod={period}", text)
        self.assertIn("now-s.lastControlDragUpdate_>=25", text)
        self.assertIn("delta>2||delta<-2", text)
        self.assertIn("const bool open=s.controlPull_.finish(now)", text)
        self.assertIn("s.settleControlCenter(open)", text)
        self.assertIn("if(!urgent&&millis()-lastUi<uiPeriod)return", text)
        self.assertIn("const bool urgent=idle!=appliedIdleStage_", text)

    def test_browser_stops_and_restarts_polling(self):
        text = self.source("src/apps/browser/BrowserApp.cpp")
        self.assertLess(text.index("takeResult(page)"), text.index("lv_timer_pause(timer)"))
        self.assertIn("lv_timer_resume(pollTimer_)", text)
        self.assertIn("lv_timer_reset(pollTimer_)", text)

    def test_adaptive_pages(self):
        files = self.source("src/apps/files/FileExplorerApp.cpp")
        accounts = self.source("src/ui/AccountDataUi.cpp")
        self.assertIn("(p.loading||p.directory||p.text||(p.image&&!p.imageShown))?200:2000", files)
        self.assertIn("(s->busy.load()||s->ready.load())?200:2000", accounts)
        self.assertIn("lv_timer_reset(page.timer)", accounts)
        self.assertGreaterEqual(files.count("lv_timer_reset(p.timer)"), 3)
