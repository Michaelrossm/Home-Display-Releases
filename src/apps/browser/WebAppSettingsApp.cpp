#include "WebAppSettingsApp.h"
#include "WebAppManager.h"
#include "../../ui/UiTheme.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/UiShell.h"
#include "../../services/IdleService.h"

namespace hp {
namespace {

const char* kindName(const IApp* app) {
    if (!app) return "";
    switch (app->kind()) {
        case AppKind::Lua: return "Lua-App";
        case AppKind::Native: return "Native App";
        case AppKind::System: return "System";
    }
    return "";
}

}

void WebAppSettingsApp::open(lv_obj_t* parent) {
    IdleService::instance().setContext(IdleContext::Normal);
    lv_obj_clean(parent);

    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    rebuild();
}

void WebAppSettingsApp::rebuild() {
    if (!root_) return;
    lv_obj_clean(root_);

    auto* card = lv_obj_create(root_);
    lv_obj_set_size(card, 540, 610);
    lv_obj_align(card, LV_ALIGN_LEFT_MID, 15, 0);
    SettingsWidgets::card(card);

    auto* title = lv_label_create(card);
    lv_label_set_text(title, "Web-App hinzufuegen");
    lv_obj_set_style_text_font(title, fontWithGermanFallback(&lv_font_montserrat_24), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 15);

    auto* nlab = lv_label_create(card);
    lv_label_set_text(nlab, "Name");
    lv_obj_align(nlab, LV_ALIGN_TOP_LEFT, 20, 75);

    name_ = lv_textarea_create(card);
    lv_obj_set_size(name_, 480, 55);
    lv_obj_align(name_, LV_ALIGN_TOP_LEFT, 20, 105);
    lv_textarea_set_one_line(name_, true);
    lv_textarea_set_max_length(name_, 39);
    lv_textarea_set_placeholder_text(name_, "z.B. IR Tracker");
    SettingsWidgets::darkInput(name_);

    auto* ulab = lv_label_create(card);
    lv_label_set_text(ulab, "Adresse");
    lv_obj_align(ulab, LV_ALIGN_TOP_LEFT, 20, 185);

    url_ = lv_textarea_create(card);
    lv_obj_set_size(url_, 480, 65);
    lv_obj_align(url_, LV_ALIGN_TOP_LEFT, 20, 215);
    lv_textarea_set_one_line(url_, true);
    lv_textarea_set_max_length(url_, 159);
    lv_textarea_set_placeholder_text(url_, "http://192.168.178.50/");
    SettingsWidgets::darkInput(url_);

    auto* add = lv_button_create(card);
    lv_obj_set_size(add, 220, 58);
    lv_obj_align(add, LV_ALIGN_TOP_LEFT, 20, 310);
    UiTheme::instance().stylePrimaryButton(add);
    lv_obj_add_event_cb(add, addCb, LV_EVENT_CLICKED, this);

    auto* al = lv_label_create(add);
    lv_label_set_text(al, LV_SYMBOL_PLUS "  Hinzufuegen");
    lv_obj_center(al);

    status_ = lv_label_create(card);
    lv_obj_set_width(status_, 480);
    lv_label_set_text(
        status_,
        "Lua-Apps werden automatisch aus /Apps/32/<id>/main.lua erkannt.\n"
        "Web-Apps sind nur ein App-Typ und werden getrennt gespeichert."
    );
    lv_obj_align(status_, LV_ALIGN_TOP_LEFT, 20, 395);

    auto* list = lv_obj_create(root_);
    lv_obj_set_size(list, 650, 610);
    lv_obj_align(list, LV_ALIGN_RIGHT_MID, -15, 0);
    SettingsWidgets::card(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    auto* lh = lv_label_create(list);
    lv_label_set_text(lh, "Installierte Apps");
    lv_obj_set_style_text_font(lh, fontWithGermanFallback(&lv_font_montserrat_24), 0);

    auto& am = AppManager::instance();
    for (size_t i = 0; i < am.count(); ++i) {
        auto* app = am.at(i);
        if (!app || !app->userInstallable()) continue;

        auto* row = lv_obj_create(list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 76);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, 0);

        auto* txt = lv_label_create(row);
        String line = String(app->name()) + "\n" + kindName(app);
        lv_label_set_text(txt, line.c_str());
        lv_obj_align(txt, LV_ALIGN_LEFT_MID, 10, 0);

        auto* sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
        if (am.isEnabled(app)) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, nativeToggleCb, LV_EVENT_VALUE_CHANGED, app);

        if (app->hasSettings()) {
            auto* settings = lv_button_create(row);
            lv_obj_set_size(settings, 54, 44);
            lv_obj_align(settings, LV_ALIGN_RIGHT_MID, -92, 0);
            UiTheme::instance().styleCard(settings);
            lv_obj_add_event_cb(settings, luaSettingsCb, LV_EVENT_CLICKED, app);
            auto* gear = lv_label_create(settings);
            lv_label_set_text(gear, LV_SYMBOL_SETTINGS);
            lv_obj_center(gear);
        }
    }

    auto* wh = lv_label_create(list);
    lv_label_set_text(wh, "Web-Apps");
    lv_obj_set_style_text_font(wh, fontWithGermanFallback(&lv_font_montserrat_24), 0);

    auto& mgr = WebAppManager::instance();
    for (size_t i = 0; i < mgr.count(); ++i) {
        const auto* r = mgr.at(i);
        if (!r) continue;

        auto* row = lv_obj_create(list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 78);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, 0);

        auto* txt = lv_label_create(row);
        String line = String(r->name) + "\n" + r->url;
        lv_label_set_text(txt, line.c_str());
        lv_obj_set_width(txt, 500);
        lv_obj_align(txt, LV_ALIGN_LEFT_MID, 10, 0);

        auto* del = lv_button_create(row);
        lv_obj_set_size(del, 54, 48);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_add_event_cb(del, deleteCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        auto* dl = lv_label_create(del);
        lv_label_set_text(dl, LV_SYMBOL_TRASH);
        lv_obj_center(dl);
    }
}

void WebAppSettingsApp::addCb(lv_event_t* e) {
    auto* self = static_cast<WebAppSettingsApp*>(lv_event_get_user_data(e));
    if (!self) return;

    const char* name = lv_textarea_get_text(self->name_);
    const char* url = lv_textarea_get_text(self->url_);

    String reason;
    if (!WebAppManager::instance().validateUrl(url, &reason)) {
        lv_label_set_text(self->status_, reason.c_str());
        return;
    }

    if (!WebAppManager::instance().add(name, url)) {
        lv_label_set_text(self->status_, "Web-App konnte nicht gespeichert werden");
        return;
    }

    self->rebuild();
}

void WebAppSettingsApp::deleteCb(lv_event_t* e) {
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    WebAppManager::instance().remove(index);
}

void WebAppSettingsApp::luaSettingsCb(lv_event_t* e) {
    auto* app = static_cast<IApp*>(lv_event_get_user_data(e));
    if (!app || !app->hasSettings()) return;
    AppManager::instance().openSettings(app->id(), UiShell::instance().content());
    IdleService::instance().activity();
}

void WebAppSettingsApp::nativeToggleCb(lv_event_t* e) {
    auto* app = static_cast<IApp*>(lv_event_get_user_data(e));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!app || !sw) return;

    AppManager::instance().setEnabled(
        app,
        lv_obj_has_state(sw, LV_STATE_CHECKED)
    );
    UiShell::instance().appsChanged();
    IdleService::instance().activity();
}

}
