from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/ui/UiShell.cpp").read_text(encoding="utf-8")
compact = "".join(source.split())
header = (ROOT / "src/ui/UiShell.h").read_text(encoding="utf-8")

assert "lv_indev_add_event_cb(indev, touchActivity, LV_EVENT_PRESSED, this)" in source
assert "IdleService::instance().stage()!=IdleStage::Off" in compact
assert "IdleService::instance().activity();" in source
assert "static void touchActivity(lv_event_t* e);" in header
print("PASS touch wakes dimmed and saver display immediately")
