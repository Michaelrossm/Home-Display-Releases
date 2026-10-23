#include "LicenseUi.h"
#include "SettingsWidgets.h"
#include "UiTheme.h"
#include "BuildInfo.h"

extern const unsigned char projectLicense[] asm("_binary_LICENSE_md_start");
extern const unsigned char thirdPartyLicense[] asm("_binary_THIRD_PARTY_NOTICES_md_start");

namespace hp::LicenseUi {
const char* projectText(){return reinterpret_cast<const char*>(projectLicense);}
const char* thirdPartyText(){return reinterpret_cast<const char*>(thirdPartyLicense);}
namespace {
void open(lv_event_t* event){
    const auto* text=static_cast<const char*>(lv_event_get_user_data(event));
    auto* overlay=lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay,LV_PCT(100),LV_PCT(100));
    UiTheme::instance().styleScreen(overlay);
    lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(overlay,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(overlay,16,0);
    auto* close=lv_button_create(overlay);
    UiTheme::instance().stylePrimaryButton(close);
    auto* caption=lv_label_create(close);lv_label_set_text(caption,"Schließen");
    lv_obj_add_event_cb(close,[](lv_event_t* e){
        lv_obj_delete_async(static_cast<lv_obj_t*>(lv_event_get_user_data(e)));
    },LV_EVENT_CLICKED,overlay);
    auto* scroll=SettingsWidgets::content(overlay,true);
    lv_obj_set_height(scroll,0);
    auto* label=lv_label_create(scroll);
    lv_obj_set_width(label,LV_PCT(100));
    lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    SettingsWidgets::readableText(label,&lv_font_montserrat_18);
    // Embedded read-only text stays in flash; no large RAM copy or storage I/O.
    lv_label_set_text_static(label,text);
}
void button(lv_obj_t* parent,const char* caption,const char* text){
    auto* b=lv_button_create(parent);UiTheme::instance().styleCard(b);
    auto* l=lv_label_create(b);lv_label_set_text(l,caption);
    lv_obj_add_event_cb(b,open,LV_EVENT_CLICKED,const_cast<char*>(text));
}
}
void build(lv_obj_t* parent){
    SettingsWidgets::label(parent,"Über Home-Display · Lizenzen",0);
    const String version=String("Home-Display ")+hp::build::kVersion+" · "+hp::build::kProfile;
    SettingsWidgets::label(parent,version.c_str(),0);
    SettingsWidgets::label(parent,"Copyright 2026 Michael Roßmann · PolyForm Noncommercial 1.0.0",0,&lv_font_montserrat_14);
    button(parent,"Projektlizenz anzeigen",projectText());
    button(parent,"Drittanbieter und Lizenzhinweise",thirdPartyText());
}
}
