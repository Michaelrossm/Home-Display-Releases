#pragma once
#include <lvgl.h>
#include <cstring>
#include "UiTheme.h"
#include "ThemeCatalog.h"
#include "ThemeController.h"
#include "ThemeControls.h"
#include "UiShell.h"
#include "../services/SettingsService.h"
#include "../services/IdleService.h"
#include "../services/LanguageService.h"

namespace hp::SettingsWidgets {

inline void readableText(lv_obj_t* obj, const lv_font_t* font = nullptr) {
    if (!obj) return;
    lv_obj_set_style_text_color(obj, UiTheme::instance().textPrimary(), 0);
    if (font) lv_obj_set_style_text_font(obj, fontWithGermanFallback(font), 0);
}

inline lv_obj_t* label(lv_obj_t* parent, const char* text, int width,
                       const lv_font_t* font = &lv_font_montserrat_18) {
    auto* result = lv_label_create(parent);
    lv_label_set_text(result, LanguageService::instance().text(text));
    if (width > 0) lv_obj_set_width(result, width);
    readableText(result, font);
    return result;
}

inline void darkInput(lv_obj_t* obj) {
    if (!obj) return;
    UiTheme::instance().styleInput(obj);
}

inline void transparent(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_text_color(obj, UiTheme::instance().textPrimary(), 0);
}

inline lv_obj_t* content(lv_obj_t* parent, bool scrollable = true) {
    auto* obj = lv_obj_create(parent);
    transparent(obj);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_flex_grow(obj, 1);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, 18, 0);
    lv_obj_set_style_pad_left(obj, 4, 0);
    lv_obj_set_style_pad_right(obj, 8, 0);
    if (scrollable) {
        lv_obj_set_scroll_dir(obj, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    return obj;
}

inline lv_obj_t* row(lv_obj_t* parent, int height = 58) {
    auto* obj = lv_obj_create(parent);
    transparent(obj);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, height);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 18, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

inline void injectThemeControls(lv_obj_t* parent) {
    if (!parent) return;

    auto* heading = lv_label_create(parent);
    lv_label_set_text(heading, "Theme & Farben");
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_style_text_font(heading, fontWithGermanFallback(&lv_font_montserrat_18), 0);
    lv_obj_set_style_text_color(heading, UiTheme::instance().textSecondary(), 0);

    auto* modeRow = row(parent, 64);
    label(modeRow, "Theme", 300);
    auto* mode = lv_dropdown_create(modeRow);
    ThemeControls::bindSelection(mode, false);
    lv_obj_set_size(mode, 360, 46);
    darkInput(mode);

    auto* accentRow = row(parent, 64);
    label(accentRow, "Akzentfarbe", 300);
    auto* accent = lv_dropdown_create(accentRow);
    ThemeControls::bindSelection(accent, true);
    lv_obj_set_size(accent, 300, 46);
    darkInput(accent);

    auto* hint = lv_label_create(parent);
    lv_label_set_text(
        hint,
        "8 integrierte Themes. Zusaetzliche .theme-Dateien auf SD unter "
        "/homepanel/themes werden beim Oeffnen dieser Seite automatisch erkannt."
    );
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, UiTheme::instance().textSecondary(), 0);
    lv_obj_set_style_text_font(hint, fontWithGermanFallback(&lv_font_montserrat_14), 0);
}

inline lv_obj_t* section(lv_obj_t* parent, const char* text) {
    if (text && std::strcmp(text, "Hintergrundbild") == 0)
        injectThemeControls(parent);

    auto* result = label(parent, text, 0, &lv_font_montserrat_18);
    lv_obj_set_width(result, LV_PCT(100));
    lv_obj_set_style_text_color(result, UiTheme::instance().textSecondary(), 0);
    return result;
}

inline lv_obj_t* field(lv_obj_t* parent, const char* text, int width = 300) {
    return label(parent, text, width, &lv_font_montserrat_18);
}

inline void card(lv_obj_t* obj) {
    if (!obj) return;
    UiTheme::instance().styleGlassPanel(obj);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, UiTheme::instance().textPrimary(), 0);
}

inline void navButton(lv_obj_t* obj, bool selected) {
    if (!obj) return;
    if (selected) UiTheme::instance().stylePrimaryButton(obj);
    else {
        UiTheme::instance().styleCard(obj);
        lv_obj_set_style_bg_color(obj, UiTheme::instance().card(), 0);
    }
}

} // namespace hp::SettingsWidgets
