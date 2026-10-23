from pathlib import Path
r = Path(__file__).resolve().parents[1]
reg = (r / "src/apps/lua/LuaAppRegistry.cpp").read_text(encoding="utf-8")
ui = (r / "src/ui/UiShell.cpp").read_text(encoding="utf-8")
assert "scanRoot(*fs" in reg
assert "xTaskGetCurrentTaskHandle() == scanTask_" in reg
assert "registerDiscovered(local[i])" in reg
assert "LuaAppRegistry::instance().discoverAndRegister()" not in ui
assert "LuaAppRegistry::instance().applyBackgroundScan()" in ui
print("Lua worker static threading checks: OK")
