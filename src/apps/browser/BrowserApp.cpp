#include "BrowserApp.h"
#include "MiniBrowserService.h"
#include "../../services/IdleService.h"
#include "../../ui/UiTheme.h"

namespace hp {

static BrowserApp* gBrowser = nullptr;

bool BrowserApp::begin() { gBrowser=this; return WebAppManager::instance().begin() && MiniBrowserService::instance().begin(); }

void BrowserApp::open(lv_obj_t* parent) {
    IdleService::instance().setContext(IdleContext::Normal);
    lv_obj_clean(parent);

    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root_, 0, 0);

    auto* bar = lv_obj_create(root_);
    lv_obj_set_size(bar, LV_PCT(100), 70);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    UiTheme::instance().styleGlassPanel(bar);

    urlLabel_ = lv_label_create(bar);
    lv_label_set_text(urlLabel_, "MiniBrowser");
    lv_obj_set_width(urlLabel_, 850);
    lv_label_set_long_mode(urlLabel_, LV_LABEL_LONG_DOT);
    lv_obj_align(urlLabel_, LV_ALIGN_LEFT_MID, 20, 0);

    auto* reload = lv_button_create(bar);
    lv_obj_set_size(reload, 58, 48);
    lv_obj_align(reload, LV_ALIGN_RIGHT_MID, -75, 0);
    auto* rl = lv_label_create(reload);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_center(rl);
    lv_obj_add_event_cb(reload, reloadCb, LV_EVENT_CLICKED, this);

    auto* save = lv_button_create(bar);
    lv_obj_set_size(save, 58, 48);
    lv_obj_align(save, LV_ALIGN_RIGHT_MID, -8, 0);
    auto* sl = lv_label_create(save);
    lv_label_set_text(sl, LV_SYMBOL_PLUS);
    lv_obj_center(sl);
    lv_obj_add_event_cb(save, saveCb, LV_EVENT_CLICKED, this);

    auto* pane = lv_obj_create(root_);
    lv_obj_set_size(pane, LV_PCT(100), 620);
    lv_obj_align(pane, LV_ALIGN_BOTTOM_MID, 0, 0);
    UiTheme::instance().styleGlassPanel(pane);
    lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);

    title_ = lv_label_create(pane);
    lv_obj_set_style_text_font(title_, fontWithGermanFallback(&lv_font_montserrat_24), 0);

    body_ = lv_label_create(pane);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_, fontWithGermanFallback(&lv_font_montserrat_18), 0);

    lv_label_set_text(title_, "MiniBrowser");
    lv_label_set_text(
        body_,
        "Gespeicherte lokale Webseiten erscheinen auf dem Home-Bildschirm als Apps.\n\n"
        "Der MiniBrowser rendert bewusst keine vollstaendige JavaScript/CSS-Webseite. "
        "Er ist fuer lokale Status-, Informations- und Geraeteseiten gedacht. "
        "Fuer IR-Tracker kann spaeter ein nativer API-Adapter dieselbe gespeicherte URL nutzen."
    );
}

void BrowserApp::render(const char* url) {
    if (!root_ || !url) return;
    currentUrl_ = url;
    lv_label_set_text(urlLabel_, currentUrl_.c_str());
    lv_label_set_text(title_, "Laden...");
    lv_label_set_text(body_, "Netzwerk wird im NetworkWorker verarbeitet. Die UI bleibt bedienbar.");
    if (!MiniBrowserService::instance().request(url)) { lv_label_set_text(title_, "Anfrage konnte nicht gestartet werden"); return; }
    if (!pollTimer_) { pollTimer_ = lv_timer_create(pollCb, 100, this); }
    else { lv_timer_resume(pollTimer_); lv_timer_reset(pollTimer_); }
}

void BrowserApp::pollCb(lv_timer_t* timer) {
    auto* self = static_cast<BrowserApp*>(lv_timer_get_user_data(timer));
    if(!self || !self->root_) return;
    MiniPage page;
    if(!MiniBrowserService::instance().takeResult(page)) return;
    lv_timer_pause(timer);
    if(!page.ok){ lv_label_set_text(self->title_, "Webseite konnte nicht geladen werden"); String e=page.error+"\n\n"+page.url; lv_label_set_text(self->body_,e.c_str()); }
    else { self->currentTitle_=page.title; lv_label_set_text(self->title_,page.title.c_str()); lv_label_set_text(self->body_,page.text.c_str()); }
}

void BrowserApp::openSaved(size_t index) {
    const auto* r = WebAppManager::instance().at(index);
    if (!r) return;
    render(r->url);
}

void BrowserApp::reloadCb(lv_event_t* e) {
    auto* self = static_cast<BrowserApp*>(lv_event_get_user_data(e));
    if (self && self->currentUrl_.length()) self->render(self->currentUrl_.c_str());
}

void BrowserApp::saveCb(lv_event_t* e) {
    auto* self = static_cast<BrowserApp*>(lv_event_get_user_data(e));
    if (!self || !self->currentUrl_.length()) return;

    String name = self->currentTitle_.length() ? self->currentTitle_ : "Web App";
    if (name.length() > 39) name.remove(39);
    WebAppManager::instance().add(name.c_str(), self->currentUrl_.c_str());
}

}
