from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
display = (ROOT / "src/hardware/DisplayManager.cpp").read_text(encoding="utf-8")
shell = (ROOT / "src/ui/UiShell.cpp").read_text(encoding="utf-8")

# Changed regions remain partial; CPU rotation is selected in the build overlay.
assert ".tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL" in display
assert '" tear=triple_partial"' in display
assert "ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL" not in display
assert "ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL" not in display

# Frequently-updated shell values should invalidate only when their content changes.
assert "if (label && value != lv_label_get_text(label)) lv_label_set_text(label, value.c_str());" in shell
assert "setLabelIfChanged(clock_, TimeService::instance().timeText());" in shell
assert "setLabelIfChanged(date_, TimeService::instance().dateText());" in shell
assert "lv_obj_invalidate(lv_screen_active())" not in shell
assert "lv_refr_now(" not in shell
print("PASS rotated display uses partial rendering")
