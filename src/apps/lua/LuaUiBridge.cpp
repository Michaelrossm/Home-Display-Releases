#include "LuaUiBridge.h"
#include "LuaApp.h"
#include "../../services/UiPerformanceMonitor.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <lvgl.h>
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include "../../ui/UiTheme.h"
#include "../../ui/InputKeyboard.h"
namespace hp {
namespace {
lv_obj_t* obj(lua_State* L, int index) {
    auto* object=static_cast<lv_obj_t*>(lua_touserdata(L,index));
    auto* app=LuaApp::self(L);
    if(!object||!app||!lv_obj_is_valid(object))return nullptr;
    for(auto* parent=object;parent;parent=lv_obj_get_parent(parent))if(parent==app->screenRoot())return object;
    return nullptr;
}

const lv_font_t* fontFor(int size) {
    switch (size) {
        case 14: return fontWithGermanFallback(&lv_font_montserrat_14);
        case 24: return fontWithGermanFallback(&lv_font_montserrat_24);
        case 28: return fontWithGermanFallback(&lv_font_montserrat_28);
        case 32: return fontWithGermanFallback(&lv_font_montserrat_32);
        case 48: return fontWithGermanFallback(&lv_font_montserrat_48);
        default: return fontWithGermanFallback(&lv_font_montserrat_18);
    }
}

lv_color_t toneColor(const char* tone) {
    if (!tone || !strcmp(tone, "primary")) return UiTheme::instance().textPrimary();
    if (!strcmp(tone, "accent")) return UiTheme::instance().accent();
    if (!strcmp(tone, "positive")) return lv_color_hex(0x6EE7B7);
    if (!strcmp(tone, "warning")) return lv_color_hex(0xF4B860);
    return UiTheme::instance().textSecondary();
}

int bridgeMillis(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}
int bridgeTickInterval(lua_State* L){
    auto* app=LuaApp::self(L);const auto interval=luaL_checkinteger(L,1);
    lua_pushboolean(L,app&&interval>=100&&interval<=2000&&app->setTickInterval(static_cast<uint32_t>(interval)));
    return 1;
}

int bridgeContainer(lua_State* L) {
    auto* parent = obj(L, 1);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* c = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lua_pushlightuserdata(L, c);
    return 1;
}

int bridgePanel(lua_State* L) {
    auto* parent = obj(L, 1);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* panel = lv_obj_create(parent);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    UiTheme::instance().styleGlassPanel(panel);
    lua_pushlightuserdata(L, panel);
    return 1;
}

int bridgeHidden(lua_State* L) {
    auto* o = obj(L, 1);
    const bool hidden = lua_toboolean(L, 2);
    if (o) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    return 0;
}

int bridgeEnabled(lua_State* L) {
    auto* o = obj(L, 1);
    if (o) {
        if (lua_toboolean(L, 2)) lv_obj_remove_state(o, LV_STATE_DISABLED);
        else lv_obj_add_state(o, LV_STATE_DISABLED);
        lv_obj_set_style_opa(o,LV_OPA_50,LV_STATE_DISABLED);
    }
    return 0;
}
int bridgeChecked(lua_State* L) {
    auto* o = obj(L, 1);
    lua_pushboolean(L, o && lv_obj_has_state(o, LV_STATE_CHECKED));
    return 1;
}
int bridgePasswordMode(lua_State* L) {
    if (auto* o = obj(L, 1); o && lv_obj_check_type(o,&lv_textarea_class)) lv_textarea_set_password_mode(o, lua_toboolean(L, 2));
    return 0;
}
int bridgeSlider(lua_State* L) {
    auto* parent = obj(L, 1);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* slider = lv_slider_create(parent);
    const int lo = std::max(-32768, std::min(32767, (int)luaL_checkinteger(L, 2)));
    const int hi = std::max(lo, std::min(32767, (int)luaL_checkinteger(L, 3)));
    lv_slider_set_range(slider, lo, hi);
    lv_slider_set_value(slider, (int)luaL_checkinteger(L, 4), LV_ANIM_OFF);
    lua_pushlightuserdata(L, slider);
    return 1;
}
int bridgeSliderValue(lua_State* L) {
    auto* o = obj(L, 1);
    lua_pushinteger(L, o && lv_obj_check_type(o,&lv_slider_class) ? lv_slider_get_value(o) : 0);
    return 1;
}
int bridgeSetSliderValue(lua_State* L) {
    if (auto* o = obj(L, 1); o && lv_obj_check_type(o,&lv_slider_class)) {
        const int value = (int)luaL_checkinteger(L, 2);
        if (!lv_obj_has_state(o, LV_STATE_PRESSED) && lv_slider_get_value(o) != value)
            lv_slider_set_value(o, value, LV_ANIM_OFF);
    }
    return 0;
}
int bridgeScrollable(lua_State* L) {
    if (auto* o = obj(L, 1)) {
        lv_obj_set_scroll_dir(o, LV_DIR_VER);
        if(lua_toboolean(L,2))lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    }
    return 0;
}
int bridgePadding(lua_State* L) {
    if (auto* o = obj(L, 1)) lv_obj_set_style_pad_all(o, LV_CLAMP(0,(int)luaL_checkinteger(L, 2),128), 0);
    return 0;
}

int bridgeTextStyle(lua_State* L) {
    auto* o = obj(L, 1);
    const int size = (int)luaL_optinteger(L, 2, 18);
    const char* tone = luaL_optstring(L, 3, "primary");
    if (o) {
        lv_obj_set_style_text_font(o, fontFor(size), 0);
        lv_obj_set_style_text_color(o, toneColor(tone), 0);
        lv_obj_set_style_text_opa(o, LV_OPA_COVER, 0);
    }
    return 0;
}

int bridgeButtonPrimary(lua_State* L) {
    auto* o = obj(L, 1);
    const bool primary = lua_toboolean(L, 2);
    if (o) {
        if (primary) UiTheme::instance().stylePrimaryButton(o);
        else UiTheme::instance().styleCard(o);
    }
    return 0;
}

int bridgeButtonText(lua_State* L) {
    auto* button = obj(L, 1);
    const char* text = luaL_optstring(L, 2, "");
    if (button && lv_obj_get_child_count(button) > 0) {
        auto* label = lv_obj_get_child(button, 0);
        if (lv_obj_check_type(label,&lv_label_class) && strlen(text)<=2048){const bool changed=strcmp(lv_label_get_text(label),text)!=0;UiPerformanceMonitor::instance().widgetUpdate(changed);if(changed)lv_label_set_text(label,text);}
    }
    return 0;
}

int bridgeTextarea(lua_State* L) {
    auto* parent = obj(L, 1);
    const char* text = luaL_optstring(L, 2, "");
    const bool password = lua_toboolean(L, 3);
    const int maxLen = LV_CLAMP(1,(int)luaL_optinteger(L, 4, 96),2048);
    keyboardInput::Mode mode;
    if(!keyboardInput::parse(luaL_optstring(L,5,"text"),mode)) return luaL_error(L,"invalid input type");
    if (!parent) { lua_pushnil(L); return 1; }
    auto* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_max_length(ta, maxLen > 0 ? (uint32_t)maxLen : 96U);
    lv_textarea_set_text(ta, text);
    InputKeyboard::setMode(ta,mode);
    UiTheme::instance().styleInput(ta);
    lv_obj_set_style_text_font(ta, fontFor(18), LV_PART_MAIN);
    lua_pushlightuserdata(L, ta);
    return 1;
}

int bridgeTextareaGet(lua_State* L) {
    auto* ta = obj(L, 1);
    lua_pushstring(L, ta && lv_obj_check_type(ta,&lv_textarea_class) ? lv_textarea_get_text(ta) : "");
    return 1;
}

int bridgeTextareaSet(lua_State* L) {
    auto* ta = obj(L, 1);
    const char* text = luaL_optstring(L, 2, "");
    if (ta && lv_obj_check_type(ta,&lv_textarea_class) && strlen(text)<=2048){const bool changed=strcmp(lv_textarea_get_text(ta),text)!=0;UiPerformanceMonitor::instance().widgetUpdate(changed);if(changed)lv_textarea_set_text(ta,text);}
    return 0;
}

int bridgeInputType(lua_State* L) {
    auto* field=obj(L,1);keyboardInput::Mode mode;
    if(!keyboardInput::parse(luaL_checkstring(L,2),mode)) return luaL_error(L,"invalid input type");
    InputKeyboard::setMode(field,mode);
    return 0;
}

void keyboardEvent(lv_event_t* e) {
    const auto code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    auto* kb = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!kb) return;
    InputKeyboard::hide(kb);
}

int bridgeKeyboard(lua_State* L) {
    auto* parent = obj(L, 1);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* kb = lv_keyboard_create(parent);
    if(!InputKeyboard::prepare(kb)){lv_obj_delete(kb);lua_pushnil(L);return 1;}
    lv_obj_set_size(kb, LV_PCT(100), 285);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(kb, keyboardEvent, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(kb, keyboardEvent, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lua_pushlightuserdata(L, kb);
    return 1;
}

int bridgeKeyboardBind(lua_State* L) {
    auto* kb = obj(L, 1);
    auto* ta = obj(L, 2);
    if (kb && ta && lv_obj_check_type(kb,&lv_keyboard_class) && lv_obj_check_type(ta,&lv_textarea_class)) {
        InputKeyboard::bind(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb);
        // The shared floating editor keeps the active value visible even when
        // a legacy/non-scrollable form cannot shrink above the keyboard.
        lv_obj_update_layout(lv_obj_get_parent(kb));
        lv_obj_scroll_to_view_recursive(ta,LV_ANIM_OFF);
    }
    return 0;
}

int bridgeKeyboardHide(lua_State* L) {
    auto* kb = obj(L, 1);
    if (kb && lv_obj_check_type(kb,&lv_keyboard_class)) {
        InputKeyboard::hide(kb);
    }
    return 0;
}

int bridgeChart(lua_State* L) {
    auto* parent = obj(L, 1);
    if (!parent) { lua_pushnil(L); return 1; }
    auto* chart = lv_chart_create(parent);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_div_line_count(chart, 5, 6);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0A1625), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 14, 0);
    lv_chart_add_series(chart, UiTheme::instance().accent(), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_add_series(chart, lv_color_hex(0x6EE7B7), LV_CHART_AXIS_PRIMARY_Y);
    lua_pushlightuserdata(L, chart);
    return 1;
}

int32_t chartValue(lua_State* L, int tableIndex, int item) {
    lua_rawgeti(L, tableIndex, item);
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(v)) return 0;
    if (v < -1000000000.0) return -1000000000;
    if (v > 1000000000.0) return 1000000000;
    return (int32_t)llround(v);
}

int bridgeChartSet(lua_State* L) {
    auto* chart = obj(L, 1);
    if (!chart || !lv_obj_check_type(chart,&lv_chart_class) || !lua_istable(L, 2) || !lua_istable(L, 3)) return 0;
    const int n = (int)std::min<size_t>(256,std::min(lua_rawlen(L, 2), lua_rawlen(L, 3)));
    if (n <= 0) return 0;
    bool changed=lv_chart_get_point_count(chart)!=n;
    if(changed)lv_chart_set_point_count(chart, n);
    auto* s1 = lv_chart_get_series_next(chart, nullptr);
    auto* s2 = s1 ? lv_chart_get_series_next(chart, s1) : nullptr;
    if (!s1 || !s2) return 0;
    int32_t minValue=0,maxValue = 1;
    auto* first=lv_chart_get_y_array(chart,s1);auto* second=lv_chart_get_y_array(chart,s2);
    for (int i = 1; i <= n; ++i) {
        const int32_t a = chartValue(L, 2, i);
        const int32_t b = chartValue(L, 3, i);
        if(first[i-1]!=a){first[i-1]=a;changed=true;}
        if(second[i-1]!=b){second[i-1]=b;changed=true;}
        minValue=std::min(minValue,std::min(a,b));
        if (a > maxValue) maxValue = a;
        if (b > maxValue) maxValue = b;
    }
    UiPerformanceMonitor::instance().widgetUpdate(changed);
    if(changed){const int32_t pad=std::max<int32_t>(1,(maxValue-minValue)/10);
        lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,minValue-pad,maxValue+pad);lv_chart_refresh(chart);}
    return 0;
}

struct ObjectBudgetLease{LuaApp* owner;size_t count;};
int guardedUiCall(lua_State* L) {
    auto* app=LuaApp::self(L);
    if(app&&app->lifecycle()==LuaRuntimeState::BackgroundExecuting)return luaL_error(L,"UI unavailable during background execution");
    if(!app||(app->lifecycle()!=LuaRuntimeState::Foreground&&app->lifecycle()!=LuaRuntimeState::Opening))return luaL_error(L,"UI requires foreground context");
    const bool needsObject=lua_toboolean(L,lua_upvalueindex(2));
    if(needsObject) {
        auto* object=static_cast<lv_obj_t*>(lua_touserdata(L,1));
        if(!app||!object||!lv_obj_is_valid(object))return luaL_error(L,"invalid UI object");
        bool owned=false;
        for(auto* p=object;p;p=lv_obj_get_parent(p))if(p==app->screenRoot()){owned=true;break;}
        if(!owned)return luaL_error(L,"foreign UI object");
    }
    const size_t objects=lua_tointeger(L,lua_upvalueindex(3));
    if(objects&&(!app||!app->reserveUiObjects(objects)))return luaL_error(L,"UI object budget exceeded");
    const int results=lua_tocfunction(L,lua_upvalueindex(1))(L);
    if(objects){
        auto* created=results?static_cast<lv_obj_t*>(lua_touserdata(L,-results)):nullptr;
        if(!created)app->releaseUiObjects(objects);
        else {
            auto* lease=new(std::nothrow) ObjectBudgetLease{app,objects};
            if(!lease){lv_obj_delete(created);app->releaseUiObjects(objects);return luaL_error(L,"UI accounting allocation failed");}
            lv_obj_add_event_cb(created,[](lv_event_t* event){auto* value=static_cast<ObjectBudgetLease*>(lv_event_get_user_data(event));value->owner->releaseUiObjects(value->count);delete value;},LV_EVENT_DELETE,lease);
        }
    }
    return results;
}
int foregroundUi(lua_State* L){
    auto* app=LuaApp::self(L);
    if(!app||(app->lifecycle()!=LuaRuntimeState::Foreground&&app->lifecycle()!=LuaRuntimeState::Opening))return luaL_error(L,"UI requires foreground context");
    return 0;
}
int percent(lua_State* L){lua_pushinteger(L,LV_PCT(LV_CLAMP(0,luaL_checkinteger(L,1),100)));return 1;}
int contentSize(lua_State* L){lua_pushinteger(L,LV_SIZE_CONTENT);return 1;}
int requestWork(lua_State* L){if(auto* app=LuaApp::self(L))app->requestUiWork();return 0;}
int removeObject(lua_State* L){if(auto* o=obj(L,1);o&&o!=LuaApp::self(L)->screenRoot())lv_obj_delete(o);return 0;}
int moveObject(lua_State* L){if(auto* o=obj(L,1);o&&o!=LuaApp::self(L)->screenRoot())lv_obj_move_to_index(o,LV_CLAMP(0,luaL_checkinteger(L,2),255));return 0;}
void guard(lua_State* L,const char* name,bool object,size_t created=0) {
    lua_getglobal(L,name);if(!lua_isfunction(L,-1)){lua_pop(L,1);return;}
    lua_pushboolean(L,object);lua_pushinteger(L,created);lua_pushcclosure(L,guardedUiCall,3);lua_setglobal(L,name);
}


}
void registerLuaUiBridge(lua_State* L) {
    lua_register(L,"hp_ui_assert_foreground",foregroundUi);
    lua_register(L,"hp_ui_percent",percent);
    lua_register(L,"hp_ui_content_size",contentSize);
    lua_register(L,"hp_ui_request_work",requestWork);
    lua_register(L,"hp_ui_delete",removeObject);
    lua_register(L,"hp_ui_move",moveObject);
    lua_register(L, "hp_panel", bridgeContainer);
    lua_register(L, "hp_set_hidden", bridgeHidden);
    lua_register(L, "hp_set_enabled", bridgeEnabled);
    lua_register(L, "hp_checked", bridgeChecked);
    lua_register(L, "hp_textarea", bridgeTextarea);
    lua_register(L, "hp_textarea_text", bridgeTextareaGet);
    lua_register(L, "hp_set_textarea_text", bridgeTextareaSet);
    lua_register(L, "hp_set_password_mode", bridgePasswordMode);
    lua_register(L, "hp_set_input_type", bridgeInputType);
    lua_register(L, "hp_ui_input_type", bridgeInputType);
    lua_register(L, "hp_slider", bridgeSlider);
    lua_register(L, "hp_slider_value", bridgeSliderValue);
    lua_register(L, "hp_set_slider_value", bridgeSetSliderValue);
    lua_register(L, "hp_set_scrollable", bridgeScrollable);
    lua_register(L, "hp_set_padding", bridgePadding);
    lua_register(L, "hp_millis", bridgeMillis);
    lua_register(L, "hp_set_tick_interval", bridgeTickInterval);
    lua_register(L, "hp_ui_container", bridgeContainer);
    lua_register(L, "hp_ui_panel", bridgePanel);
    lua_register(L, "hp_ui_hidden", bridgeHidden);
    lua_register(L, "hp_ui_text_style", bridgeTextStyle);
    lua_register(L, "hp_ui_button_primary", bridgeButtonPrimary);
    lua_register(L, "hp_ui_button_text", bridgeButtonText);
    lua_register(L, "hp_ui_textarea", bridgeTextarea);
    lua_register(L, "hp_ui_textarea_get", bridgeTextareaGet);
    lua_register(L, "hp_ui_textarea_set", bridgeTextareaSet);
    lua_register(L, "hp_ui_keyboard", bridgeKeyboard);
    lua_register(L, "hp_ui_keyboard_bind", bridgeKeyboardBind);
    lua_register(L, "hp_ui_keyboard_hide", bridgeKeyboardHide);
    lua_register(L, "hp_ui_chart", bridgeChart);
    lua_register(L, "hp_ui_chart_set", bridgeChartSet);
}

void protectLuaUiBridge(lua_State* L) {
    guard(L,"hp_ui_icon_set",true);
    guard(L,"hp_ui_icon_button",true,2);guard(L,"hp_ui_icon_button_set",true);
    const char* constructors[]={"hp_label","hp_switch","hp_panel","hp_textarea","hp_slider","hp_ui_container","hp_ui_panel","hp_ui_textarea","hp_ui_chart","hp_ui_spinner","hp_ui_dropdown","hp_ui_checkbox","hp_ui_arc","hp_ui_progress","hp_ui_list","hp_ui_tabview","hp_ui_table","hp_ui_icon","hp_ui_image","hp_ui_line"};
    for(const char* name:constructors)guard(L,name,true,1);
    guard(L,"hp_ui_keyboard",true,5); // Keyboard, editor strip, textarea, eye and label.
    guard(L,"hp_ui_virtual_list",true,51);
    guard(L,"hp_ui_month_grid",true,85);
    guard(L,"hp_ui_month_set",true);guard(L,"hp_ui_month_callback",true);
    for(const char* name:{"hp_ui_virtual_items","hp_ui_virtual_callbacks","hp_ui_collection_status","hp_ui_collection_pending","hp_ui_collection_step"})guard(L,name,true);
    guard(L,"hp_button",true,2);guard(L,"hp_ui_list_item",true,3);guard(L,"hp_ui_tab",true,3);
    guard(L,"hp_chart_create",true,2);guard(L,"hp_chart_add_series",true); // Returns a series handle, not an lv_obj_t.
    for(const char* name:{"hp_chart_resize","hp_chart_set_range","hp_chart_set_cursor_labels","hp_chart_append","hp_chart_history_range"})guard(L,name,true);
    const char* setters[]={"hp_align","hp_set_size","hp_set_text","hp_set_bg_color","hp_set_text_color","hp_set_radius","hp_set_border","hp_set_font_size","hp_on_click","hp_on_value_changed","hp_on_released","hp_set_hidden","hp_set_enabled","hp_checked","hp_set_checked","hp_textarea_text","hp_set_textarea_text","hp_set_password_mode","hp_slider_value","hp_set_slider_value","hp_set_scrollable","hp_set_padding","hp_ui_hidden","hp_ui_text_style","hp_ui_button_primary","hp_ui_button_text","hp_ui_textarea_get","hp_ui_textarea_set","hp_ui_keyboard_bind","hp_ui_keyboard_hide","hp_ui_chart_set","hp_ui_dropdown_value","hp_ui_dropdown_set_value","hp_ui_checkbox_checked","hp_ui_arc_set","hp_ui_progress_set","hp_ui_table_set","hp_ui_flex","hp_ui_gap","hp_ui_grow","hp_ui_padding","hp_ui_margin","hp_ui_style_card","hp_ui_style_primary","hp_ui_style_input","hp_chart_set_series","hp_chart_set_labels"};
    for(const char* name:setters)guard(L,name,true);
    guard(L,"hp_set_input_type",true);guard(L,"hp_ui_input_type",true);
    guard(L,"hp_ui_delete",true);guard(L,"hp_ui_move",true);
    guard(L,"hp_ui_request_work",false);
    guard(L,"hp_ui_dropdown_set_options",true);
    guard(L,"hp_set_tick_interval",false);
    guard(L,"hp_ui_image_status",true);
    guard(L,"hp_ui_image_resource",true); // This constructor reserves its own object slot.
    guard(L,"hp_ui_image_set_source",true);
    guard(L,"hp_toast",false,1);guard(L,"hp_alert",false,8);
}


}
