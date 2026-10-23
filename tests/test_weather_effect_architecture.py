from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT/path).read_text(encoding="utf-8")

def test_weather_effect_is_ui_only_and_bounded():
    cpp=source("src/ui/UiAmbientAnimationService.cpp")
    hdr=source("src/ui/UiAmbientAnimationService.h")
    assert "xTaskCreate" not in cpp
    assert "BackgroundScheduler" not in cpp
    assert cpp.count("lv_timer_create(")==1
    assert "kMaxParticles=8" in hdr
    assert "kMaxEffects=32" in hdr and "kMaxOwnerEffects=16" in hdr
    assert "160" in cpp and "120" in cpp
    assert "owner->reserveUiObjects(10)" in cpp and "owner->releaseUiObjects(10)" in cpp
    assert "lv_obj_get_parent(e.root)==parent" in cpp

def test_weather_effect_respects_idle_lifecycle_and_theme():
    cpp=source("src/ui/UiAmbientAnimationService.cpp")
    lua=source("src/apps/lua/LuaApp.h")
    for token in (
        "IdleStage::Saver",
        "IdleStage::Off",
        "IdleStage::Dimmed",
        "LuaRuntimeState::Foreground",
        "UiTheme::instance().generation()",
        "EventType::IdleStateChanged",
        "EventType::AppOpened",
    ):
        assert token in cpp
    assert "uiAnimationEligible" in lua
    assert "lifecycle_==LuaRuntimeState::Foreground" in lua

def test_lua_weather_api_registered_and_no_animation_tick_contract():
    bridge=source("src/apps/lua/LuaWeatherEffectBridge.cpp")
    runtime=source("src/apps/lua/LuaApp.cpp")
    widgets=source("src/apps/lua/LuaWidgetsBridge.cpp")
    for name in ("hp_ui_weather_effect","hp_ui_weather_effect_set","hp_weather_symbol"):
        assert name in bridge
    assert runtime.count("registerLuaWeatherEffectBridge(L)")==1
    assert "registerLuaWeatherEffectBridge(L)" not in widgets
    assert 'weather effect update requires foreground UI' in bridge
    assert "on_tick" not in bridge
    animation=source("src/ui/UiAmbientAnimationService.cpp")
    assert 'createAppIcon(e->root,"@weather_cloudy"' in animation
    assert 'setAppIconSymbol(e.icon' in animation
    assert 'lv_label_set_text_static(e.icon' not in animation
    assert "LuaRuntimeState::Opening" in bridge
    assert "UiAmbientAnimationService::instance().uiStateChanged()" in runtime

def test_weather_semantic_icons_use_existing_font_symbols():
    icons=source("src/ui/IconCatalog.h")
    for token in ("partly_cloudy","drizzle","heavy_rain","storm","snow","wind","temperature","rainbow"):
        assert token in icons


class WeatherEffectArchitectureTests(unittest.TestCase):
    # dev.py uses unittest discovery; module-level pytest functions alone
    # were never executed by the project's standard test command.
    def test_bounded(self):
        test_weather_effect_is_ui_only_and_bounded()

    def test_lifecycle(self):
        test_weather_effect_respects_idle_lifecycle_and_theme()

    def test_registration(self):
        test_lua_weather_api_registered_and_no_animation_tick_contract()

    def test_icons(self):
        test_weather_semantic_icons_use_existing_font_symbols()
