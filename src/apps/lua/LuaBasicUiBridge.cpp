#include "LuaUiBridge.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include "LuaApp.h"
#include "../../services/UiPerformanceMonitor.h"
#include "../../ui/UiTheme.h"
#include <cstring>
#include <cstdint>
namespace hp {
namespace {
// Compare LOCAL overrides, not resolved theme values: the first explicit
// override must still be installed even when it currently matches the theme.
void localNumber(lv_obj_t* obj,lv_style_prop_t prop,int32_t number){
    lv_style_value_t before{},value{};value.num=number;
    const bool changed=lv_obj_get_local_style_prop(obj,prop,&before,LV_PART_MAIN)!=LV_STYLE_RES_FOUND||before.num!=number;
    UiPerformanceMonitor::instance().widgetUpdate(changed);if(changed)lv_obj_set_local_style_prop(obj,prop,value,LV_PART_MAIN);
}
void localColor(lv_obj_t* obj,lv_style_prop_t prop,lv_color_t color){
    lv_style_value_t before{},value{};value.color=color;
    const bool changed=lv_obj_get_local_style_prop(obj,prop,&before,LV_PART_MAIN)!=LV_STYLE_RES_FOUND||!lv_color_eq(before.color,color);
    UiPerformanceMonitor::instance().widgetUpdate(changed);if(changed)lv_obj_set_local_style_prop(obj,prop,value,LV_PART_MAIN);
}
void localFont(lv_obj_t* obj,const lv_font_t* font){
    lv_style_value_t before{},value{};value.ptr=font;
    const bool changed=lv_obj_get_local_style_prop(obj,LV_STYLE_TEXT_FONT,&before,LV_PART_MAIN)!=LV_STYLE_RES_FOUND||before.ptr!=font;
    UiPerformanceMonitor::instance().widgetUpdate(changed);if(changed)lv_obj_set_local_style_prop(obj,LV_STYLE_TEXT_FONT,value,LV_PART_MAIN);
}
lv_align_t parseAlign(const char* s) {
    if (!s) return LV_ALIGN_TOP_LEFT;
    if (!strcmp(s, "center")) return LV_ALIGN_CENTER;
    if (!strcmp(s, "top_right")) return LV_ALIGN_TOP_RIGHT;
    if (!strcmp(s, "top_mid")) return LV_ALIGN_TOP_MID;
    if (!strcmp(s, "bottom_left")) return LV_ALIGN_BOTTOM_LEFT;
    if (!strcmp(s, "bottom_right")) return LV_ALIGN_BOTTOM_RIGHT;
    if (!strcmp(s, "bottom_mid")) return LV_ALIGN_BOTTOM_MID;
    if (!strcmp(s, "left_mid")) return LV_ALIGN_LEFT_MID;
    if (!strcmp(s, "right_mid")) return LV_ALIGN_RIGHT_MID;
    return LV_ALIGN_TOP_LEFT;
}

lv_color_t luaHexColor(lua_State* L, int index) {
    // Lua integers are signed on this target.  Masking keeps the bridge
    // deliberately limited to the RGB values accepted by LVGL.
    const uint32_t rgb = static_cast<uint32_t>(luaL_optinteger(L, index, 0)) & 0x00FFFFFFU;
    return lv_color_hex(rgb);
}

const lv_font_t* luaFontForSize(int px) {
    // Keep this to the fonts already linked by the firmware.  The nearest
    // available size avoids allocating or loading fonts at run time.
    if (px <= 16) return fontWithGermanFallback(&lv_font_montserrat_14);
    if (px <= 21) return fontWithGermanFallback(&lv_font_montserrat_18);
    if (px <= 26) return fontWithGermanFallback(&lv_font_montserrat_24);
    if (px <= 30) return fontWithGermanFallback(&lv_font_montserrat_28);
    if (px <= 40) return fontWithGermanFallback(&lv_font_montserrat_32);
    return fontWithGermanFallback(&lv_font_montserrat_48);
}

void setTextColor(lv_obj_t* obj, lv_color_t color) {
    if (!obj) return;
    localColor(obj,LV_STYLE_TEXT_COLOR,color);
    // Buttons own a child label with an explicit default color.  Styling
    // labels below the passed object makes hp_set_text_color work for both a
    // label itself and a generic button/container without persistent work.
    const uint32_t childCount = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < childCount; ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        if (child && lv_obj_check_type(child, &lv_label_class))
            localColor(child,LV_STYLE_TEXT_COLOR,color);
    }
}

void setTextFont(lv_obj_t* obj, const lv_font_t* font) {
    if (!obj || !font) return;
    localFont(obj,font);
    const uint32_t childCount = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < childCount; ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        if (child && lv_obj_check_type(child, &lv_label_class))
            localFont(child,font);
    }
}

int bridgeRoot(lua_State* L) {
    auto* app = LuaApp::self(L);
    lua_pushlightuserdata(L, app ? (void*)app->screenRoot() : nullptr);
    return 1;
}

int bridgeLabel(lua_State* L) {
    auto* parent = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const char* text = luaL_optstring(L, 2, "");
    if (!parent || strlen(text)>2048) { lua_pushnil(L); return 1; }
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, fontWithGermanFallback(&lv_font_montserrat_18), 0);
    lv_obj_set_style_text_color(label, UiTheme::instance().textPrimary(), 0);
    lua_pushlightuserdata(L, label);
    return 1;
}

int bridgeButton(lua_State* L) {
    auto* parent = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const char* text = luaL_optstring(L, 2, "");
    if (!parent || strlen(text)>2048) { lua_pushnil(L); return 1; }
    auto* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 220, 58);
    UiTheme::instance().styleCard(btn);
    auto* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, fontWithGermanFallback(&lv_font_montserrat_18), 0);
    lv_obj_center(label);
    lua_pushlightuserdata(L, btn);
    return 1;
}

int bridgeSwitch(lua_State* L) {
    auto* parent = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const bool checked = lua_toboolean(L, 2);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* sw = lv_switch_create(parent);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lua_pushlightuserdata(L, sw);
    return 1;
}
int bridgeSetChecked(lua_State* L){
 auto* o=static_cast<lv_obj_t*>(lua_touserdata(L,1));
 if(o&&(lv_obj_check_type(o,&lv_checkbox_class)||lv_obj_check_type(o,&lv_switch_class))){if(lua_toboolean(L,2))lv_obj_add_state(o,LV_STATE_CHECKED);else lv_obj_remove_state(o,LV_STATE_CHECKED);}return 0;
}

int bridgeAlign(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const char* align = luaL_optstring(L, 2, "top_left");
    const int x = LV_CLAMP(-4096,(int)luaL_optinteger(L, 3, 0),4096);
    const int y = LV_CLAMP(-4096,(int)luaL_optinteger(L, 4, 0),4096);
    if (obj) lv_obj_align(obj, parseAlign(align), x, y);
    return 0;
}

int32_t uiDimension(int64_t value) {
    // LVGL tags percentage/content coordinates in the high bits. They are
    // not pixel counts and must not be clamped to a 4096-pixel rectangle.
    if(value==LV_SIZE_CONTENT)return LV_SIZE_CONTENT;
    if(value>=INT32_MIN && value<=INT32_MAX) {
        const auto coordinate=static_cast<int32_t>(value);
        if(LV_COORD_IS_PCT(coordinate) && LV_COORD_GET_PCT(coordinate)>=0 && LV_COORD_GET_PCT(coordinate)<=100)
            return coordinate;
    }
    return value<1?1:value>4096?4096:static_cast<int32_t>(value);
}
int bridgeSetSize(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const int w = uiDimension(luaL_optinteger(L, 2, 100));
    const int h = uiDimension(luaL_optinteger(L, 3, 50));
    if (obj){localNumber(obj,LV_STYLE_WIDTH,w);localNumber(obj,LV_STYLE_HEIGHT,h);}
    return 0;
}

int bridgeSetText(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const char* text = luaL_optstring(L, 2, "");
    // LVGL reallocates and invalidates even identical text. Polling Lua apps
    // must not repaint all their labels when the media snapshot is unchanged.
    if (obj && lv_obj_check_type(obj,&lv_label_class) && strlen(text)<=2048){
        const bool changed=strcmp(lv_label_get_text(obj), text) != 0;
        UiPerformanceMonitor::instance().widgetUpdate(changed);
        if(changed)lv_label_set_text(obj, text);
    }
    return 0;
}

int bridgeSetBgColor(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    if (obj) {
        localColor(obj,LV_STYLE_BG_COLOR,luaHexColor(L,2));
        localNumber(obj,LV_STYLE_BG_OPA,LV_OPA_COVER);
    }
    return 0;
}

int bridgeSetTextColor(lua_State* L) {
    setTextColor(static_cast<lv_obj_t*>(lua_touserdata(L, 1)), luaHexColor(L, 2));
    return 0;
}

int bridgeSetRadius(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const int radius = static_cast<int>(luaL_optinteger(L, 2, 0));
    if (obj) localNumber(obj,LV_STYLE_RADIUS,LV_CLAMP(0,radius,255));
    return 0;
}

int bridgeSetBorder(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const int width = static_cast<int>(luaL_optinteger(L, 2, 0));
    const int opacity = static_cast<int>(luaL_optinteger(L, 4, LV_OPA_COVER));
    if (obj) {
        localNumber(obj,LV_STYLE_BORDER_WIDTH,LV_CLAMP(0,width,255));
        localColor(obj,LV_STYLE_BORDER_COLOR,luaHexColor(L,3));
        localNumber(obj,LV_STYLE_BORDER_OPA,LV_CLAMP(0,opacity,LV_OPA_COVER));
    }
    return 0;
}

int bridgeSetFontSize(lua_State* L) {
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const int px = static_cast<int>(luaL_optinteger(L, 2, 18));
    setTextFont(obj, luaFontForSize(LV_CLAMP(1, px, 96)));
    return 0;
}

int bridgeOnEvent(lua_State* L, lv_event_code_t code) {
    auto* app = LuaApp::self(L);
    auto* obj = static_cast<lv_obj_t*>(lua_touserdata(L, 1));
    const char* fn = luaL_optstring(L, 2, nullptr);
    if (app && obj && fn) {
        app->bindEvent(obj, code, fn);
    }
    return 0;
}
int bridgeOnClick(lua_State* L) { return bridgeOnEvent(L, LV_EVENT_CLICKED); }
int bridgeOnValueChanged(lua_State* L) { return bridgeOnEvent(L, LV_EVENT_VALUE_CHANGED); }
int bridgeOnReleased(lua_State* L) { return bridgeOnEvent(L, LV_EVENT_RELEASED); }

}
void registerBasicLuaUiBridge(lua_State* L) {
    lua_register(L, "hp_root", bridgeRoot);
    lua_register(L, "hp_label", bridgeLabel);
    lua_register(L, "hp_button", bridgeButton);
    lua_register(L, "hp_switch", bridgeSwitch);
    lua_register(L, "hp_set_checked", bridgeSetChecked);
    lua_register(L, "hp_align", bridgeAlign);
    lua_register(L, "hp_set_size", bridgeSetSize);
    lua_register(L, "hp_set_text", bridgeSetText);
    lua_register(L, "hp_set_bg_color", bridgeSetBgColor);
    lua_register(L, "hp_set_text_color", bridgeSetTextColor);
    lua_register(L, "hp_set_radius", bridgeSetRadius);
    lua_register(L, "hp_set_border", bridgeSetBorder);
    lua_register(L, "hp_set_font_size", bridgeSetFontSize);
    lua_register(L, "hp_on_click", bridgeOnClick);
    lua_register(L, "hp_on_value_changed", bridgeOnValueChanged);
    lua_register(L, "hp_on_released", bridgeOnReleased);
}
}
