#include "ThemeControls.h"
#include "UiTheme.h"
#include "ThemeController.h"
#include "ThemeCatalog.h"
#include "../services/SettingsService.h"

namespace hp {

ThemeControls& ThemeControls::instance() {
    static ThemeControls controls;
    return controls;
}

void ThemeControls::changed(lv_event_t* e) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!dropdown) return;
    auto settings = SettingsService::instance().snapshot();
    const uintptr_t field =
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    const uint8_t value =
        static_cast<uint8_t>(lv_dropdown_get_selected(dropdown));
    if (field == 0) settings.ui.themeMode = value;
    else settings.ui.accent = value;
    ThemeController::instance().apply(
        settings.ui.themeMode, settings.ui.accent);
}

void ThemeControls::buildControlCenter(lv_obj_t* parent) {
    if (!parent) return;

    auto* heading = lv_label_create(parent);
    lv_label_set_text(heading, "Theme");
    lv_obj_set_style_text_font(
        heading, fontWithGermanFallback(&lv_font_montserrat_18), 0);
    lv_obj_set_style_text_color(
        heading, UiTheme::instance().textPrimary(), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 800, 285);

    auto* modeLabel = lv_label_create(parent);
    lv_label_set_text(modeLabel, "Design");
    lv_obj_set_style_text_color(
        modeLabel, UiTheme::instance().textSecondary(), 0);
    lv_obj_align(modeLabel, LV_ALIGN_TOP_LEFT, 800, 325);

    auto* mode = lv_dropdown_create(parent);
    mode_ = mode;
    lv_obj_add_event_cb(mode, [](lv_event_t* e) {
        *static_cast<lv_obj_t**>(lv_event_get_user_data(e)) = nullptr;
    }, LV_EVENT_DELETE, &mode_);
    bindSelection(mode, false);
    lv_obj_set_size(mode, 300, 52);
    lv_obj_align(mode, LV_ALIGN_TOP_LEFT, 800, 352);
    lv_obj_set_style_bg_color(mode, UiTheme::instance().card(), 0);
    lv_obj_set_style_border_color(mode, UiTheme::instance().border(), 0);
    lv_obj_set_style_text_color(mode, UiTheme::instance().textPrimary(), 0);

    auto* accentLabel = lv_label_create(parent);
    lv_label_set_text(accentLabel, "Akzentfarbe");
    lv_obj_set_style_text_color(
        accentLabel, UiTheme::instance().textSecondary(), 0);
    lv_obj_align(accentLabel, LV_ALIGN_TOP_LEFT, 800, 420);

    auto* accent = lv_dropdown_create(parent);
    accent_ = accent;
    lv_obj_add_event_cb(accent, [](lv_event_t* e) {
        *static_cast<lv_obj_t**>(lv_event_get_user_data(e)) = nullptr;
    }, LV_EVENT_DELETE, &accent_);
    bindSelection(accent, true);
    lv_obj_set_size(accent, 300, 52);
    lv_obj_align(accent, LV_ALIGN_TOP_LEFT, 800, 447);
    lv_obj_set_style_bg_color(accent, UiTheme::instance().card(), 0);
    lv_obj_set_style_border_color(accent, UiTheme::instance().border(), 0);
    lv_obj_set_style_text_color(accent, UiTheme::instance().textPrimary(), 0);
}

void ThemeControls::bindSelection(lv_obj_t* dropdown, bool accent) {
    auto& catalog = ThemeCatalog::instance();
    catalog.begin();
    if (!accent) catalog.refreshSd();
    syncSelection(dropdown, accent);
    lv_obj_add_event_cb(dropdown, changed, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(accent)));
}

void ThemeControls::refreshControlCenter() {
    // No filesystem scan while dragging; reflect the shared catalog and selection.
    if (mode_) syncSelection(mode_, false);
    if (accent_) syncSelection(accent_, true);
}

void ThemeControls::syncSelection(lv_obj_t* dropdown, bool accent) {
    auto& catalog = ThemeCatalog::instance();
    const auto settings = SettingsService::instance().snapshot();
    String accentOptions;
    if (accent) {
        for (uint8_t id = 0; id < ThemeCatalog::kAccentCount; ++id) {
            if (id) accentOptions += '\n';
            accentOptions += ThemeCatalog::accentName(id);
        }
    }
    lv_dropdown_set_options(dropdown, accent ? accentOptions.c_str() : catalog.optionList());
    lv_dropdown_set_selected(dropdown, accent ? (settings.ui.accent <= 5 ? settings.ui.accent : 0) : catalog.normalizeMode(UiTheme::instance().mode()));
}

}
