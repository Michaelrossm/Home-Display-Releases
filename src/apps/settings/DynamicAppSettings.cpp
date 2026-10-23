#include "DynamicAppSettings.h"
#include "../../core/AppManager.h"
#include "../../ui/UiShell.h"
#include "../../ui/UiTheme.h"
#include "../../ui/SettingsWidgets.h"
#include <cstring>

namespace hp {
namespace {

constexpr uintptr_t kMarkerValue = 0x41505053u; // "APPS"

const char* kindText(const IApp* app) {
    if (!app) return "";
    switch (app->kind()) {
        case AppKind::Lua: return "Lua-App";
        case AppKind::Native: return "Native App";
        case AppKind::System: return "System-App";
    }
    return "App";
}

}

DynamicAppSettings& DynamicAppSettings::instance() {
    static DynamicAppSettings d;
    return d;
}

lv_obj_t* DynamicAppSettings::findLabelRecursive(lv_obj_t* root, const char* text) const {
    if (!root || !text) return nullptr;

    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(root, i);
        if (!child) continue;

        if (lv_obj_check_type(child, &lv_label_class)) {
            const char* value = lv_label_get_text(child);
            if (value && std::strcmp(value, text) == 0) return child;
        }

        if (auto* found = findLabelRecursive(child, text)) return found;
    }

    return nullptr;
}

lv_obj_t* DynamicAppSettings::findAppsPanel(lv_obj_t* root) const {
    auto* title = findLabelRecursive(root, "App-Einstellungen");
    return title ? lv_obj_get_parent(title) : nullptr;
}

void DynamicAppSettings::openCb(lv_event_t* e) {
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!row) return;

    auto* app = static_cast<IApp*>(lv_obj_get_user_data(row));
    if (!app || !app->hasSettings()) return;

    auto& manager = AppManager::instance();
    if (app->userInstallable() && !manager.isEnabled(app)) return;

    UiShell::instance().requestAppSettings(app->id());
}

void DynamicAppSettings::enabledCb(lv_event_t* e) {
    auto* sw=static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* app=sw?static_cast<IApp*>(lv_obj_get_user_data(sw)):nullptr;
    if(!app||!app->userInstallable())return;
    const bool enabled=lv_obj_has_state(sw,LV_STATE_CHECKED);
    if(AppManager::instance().setEnabled(app,enabled)) {
        UiShell::instance().appsChanged();
        instance().invalidate(); // Rebuild later, not inside the LVGL callback.
    } else {
        if(AppManager::instance().isEnabled(app))lv_obj_add_state(sw,LV_STATE_CHECKED);
        else lv_obj_remove_state(sw,LV_STATE_CHECKED);
    }
}

void DynamicAppSettings::listDeletedCb(lv_event_t* e) {
    auto& self=instance();
    auto* target=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(target==self.list_) {
        self.list_=nullptr;
        self.injectedPanel_=nullptr;
        self.nextScanMs_=0;
    }
}

void DynamicAppSettings::inject(lv_obj_t* panel) {
    if (!panel) return;

    // SettingsApp still creates its three historical C++ buttons.
    // Hide every direct child except the page title. The replacement list below
    // contains native + Lua apps together, so there is only ONE scroll area.
    const uint32_t existing = lv_obj_get_child_count(panel);
    for (uint32_t i = 0; i < existing; ++i) {
        lv_obj_t* child = lv_obj_get_child(panel, i);
        if (!child) continue;

        bool keep = false;
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char* value = lv_label_get_text(child);
            keep = value && std::strcmp(value, "App-Einstellungen") == 0;
        }

        if (!keep) lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }

    list_ = lv_obj_create(panel);
    lv_obj_set_user_data(list_, reinterpret_cast<void*>(kMarkerValue));
    lv_obj_add_event_cb(list_, listDeletedCb, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(list_, 920, 555);
    lv_obj_align(list_, LV_ALIGN_TOP_LEFT, 22, 88);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_pad_all(list_, 8, 0);
    lv_obj_set_style_pad_row(list_, 8, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_SCROLL_ELASTIC);

    auto& am = AppManager::instance();
    size_t shown = 0;

    for (size_t i = 0; i < am.count(); ++i) {
        auto* app = am.at(i);
        if (!app || !app->userInstallable()) continue;

        const bool enabled = am.isEnabled(app);
        const bool settings = app->hasSettings();
        const bool usable = enabled && settings;

        auto* row = lv_button_create(list_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 76);
        SettingsWidgets::card(row);
        lv_obj_set_user_data(row, app);

        if (usable) {
            lv_obj_add_event_cb(row, openCb, LV_EVENT_CLICKED, nullptr);
        } else {
            lv_obj_set_style_opa(row, LV_OPA_50, 0);
        }

        auto* name = lv_label_create(row);
        String second = kindText(app);
        const auto runtime = am.runtimeStatus(app->id());

        if (runtime.state == AppRuntimeState::Error) {
            second += " - Fehler";
            if (runtime.detail.length()) second += ": " + runtime.detail;
        } else if (!enabled) {
            second += " - deaktiviert";
        } else if (!settings) {
            second += " - keine eigenen Einstellungen";
        }

        String label = String(app->name()) + "\n" + second;
        lv_label_set_text(name, label.c_str());
        lv_obj_set_width(name, 700);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, fontWithGermanFallback(&lv_font_montserrat_18), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);

        auto* state = lv_label_create(row);
        lv_label_set_text(
            state,
            settings ? (enabled ? LV_SYMBOL_SETTINGS "  " LV_SYMBOL_RIGHT : LV_SYMBOL_SETTINGS)
                     : "-"
        );
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, -18, 0);

        auto* toggle=lv_switch_create(row);
        if(enabled)lv_obj_add_state(toggle,LV_STATE_CHECKED);
        lv_obj_set_user_data(toggle,app);
        lv_obj_align(toggle,LV_ALIGN_RIGHT_MID,-70,0);
        lv_obj_add_event_cb(toggle,enabledCb,LV_EVENT_VALUE_CHANGED,nullptr);

        ++shown;
    }

    if (!shown) {
        auto* info = lv_label_create(list_);
        lv_label_set_text(info, "Keine verwaltbaren Apps vorhanden.");
        lv_obj_set_style_text_font(info, fontWithGermanFallback(&lv_font_montserrat_18), 0);
        lv_obj_set_style_text_color(info, UiTheme::instance().textSecondary(), 0);
        lv_obj_align(info, LV_ALIGN_TOP_LEFT, 12, 8);
    }

    injectedPanel_ = panel;
}

void DynamicAppSettings::update() {
    auto* current = AppManager::instance().current();
    if (!current || std::strcmp(current->id(), "settings") != 0) {
        // The list deletion callback clears cached LVGL pointers when the page
        // is actually destroyed. Do not traverse the UI tree outside Settings.
        return;
    }

    // Fast path: on the normal Settings UI loop the injected list is already
    // alive. This removes the former recursive full-tree search from every
    // frame/pass.
    if (list_ && !dirty_) return;
    // When another Settings page is visible there is no Apps panel to find.
    // Do not recursively scan its complete widget tree every UI pass.
    if(nextScanMs_&&static_cast<int32_t>(millis()-nextScanMs_)<0)return;
    nextScanMs_=millis()+250;

    // A deliberate invalidate only rebuilds the known live list. No recursive
    // search is needed while ownership is known.
    if (dirty_ && list_) {
        dirty_ = false;
        lv_obj_delete(list_); // callback clears both cached pointers
    }

    // Only page creation/recreation reaches this scan.
    auto* panel = findAppsPanel(UiShell::instance().content());
    if (!panel) {
        injectedPanel_ = nullptr;
        list_ = nullptr;
        return;
    }

    // SettingsApp may have rebuilt the page since the previous pass. Resolve a
    // pre-existing marked list once before creating a new one.
    for (uint32_t i = 0; i < lv_obj_get_child_count(panel); ++i) {
        auto* child = lv_obj_get_child(panel, i);
        if (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(child)) == kMarkerValue) {
            list_ = child;
            injectedPanel_ = panel;
            dirty_ = false;
            return;
        }
    }

    dirty_ = false;
    inject(panel);
}

}
