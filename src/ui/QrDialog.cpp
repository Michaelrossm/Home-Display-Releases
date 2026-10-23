#include "QrDialog.h"
#include "../services/QrGenerator.h"
#include "../services/SecurityService.h"
#include "SecurityUi.h"
#include "SettingsWidgets.h"
#include <mbedtls/platform_util.h>
namespace hp {namespace {
struct Context {lv_obj_t* root;lv_timer_t* timer=nullptr;uint32_t started;bool sensitive;bool closing=false;};
void close(Context* c){if(c->closing)return;c->closing=true;if(c->timer)lv_timer_pause(c->timer);lv_obj_delete_async(c->root);}
}
void QrDialog::show(const char* title,const String& payload,bool sensitive,lv_obj_t* owner){
    if(sensitive&&(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())){SecurityUi::requestSettingsUnlock();return;}
    auto* root=lv_obj_create(owner?owner:lv_layer_top());lv_obj_add_flag(root,LV_OBJ_FLAG_FLOATING);lv_obj_set_size(root,LV_PCT(100),LV_PCT(100));lv_obj_center(root);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_flex_flow(root,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(root,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    auto* c=new Context{root,nullptr,millis(),sensitive};
    SettingsWidgets::label(root,title,0,&lv_font_montserrat_24);
    auto* qr=lv_qrcode_create(root);lv_qrcode_set_size(qr,400);lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());lv_obj_set_style_border_width(qr,16,0);lv_obj_set_style_border_color(qr,lv_color_white(),0);
    lv_obj_add_event_cb(qr,[](lv_event_t* e){auto* draw=lv_canvas_get_draw_buf(static_cast<lv_obj_t*>(lv_event_get_target(e)));if(draw&&draw->data)mbedtls_platform_zeroize(draw->data,draw->data_size);},LV_EVENT_DELETE,nullptr);
    if(!QrGenerator::update(qr,payload)){lv_obj_add_flag(qr,LV_OBJ_FLAG_HIDDEN);SettingsWidgets::label(root,"Daten zu umfangreich für einen QR-Code. Kontakt bitte als vCard exportieren.",0,&lv_font_montserrat_18);}
    else SettingsWidgets::label(root,sensitive?"WLAN-Zugangsdaten teilen · QR enthält das Passwort":"Mit der Handykamera scannen",0,&lv_font_montserrat_18);
    auto* button=lv_button_create(root);SettingsWidgets::label(button,"Schließen",0,&lv_font_montserrat_18);lv_obj_add_event_cb(button,[](lv_event_t* e){close(static_cast<Context*>(lv_event_get_user_data(e)));},LV_EVENT_CLICKED,c);
    lv_obj_add_event_cb(root,[](lv_event_t* e){auto* c=static_cast<Context*>(lv_event_get_user_data(e));if(lv_event_get_target(e)!=c->root)return;if(c->timer)lv_timer_delete(c->timer);delete c;},LV_EVENT_DELETE,c);
    c->timer=lv_timer_create([](lv_timer_t* t){auto* c=static_cast<Context*>(lv_timer_get_user_data(t));if(uint32_t(millis()-c->started)>=60000||SecurityService::instance().tabletLocked()||(c->sensitive&&!SecurityService::instance().settingsAllowed()))close(c);},1000,c);
    if(!c->timer)close(c);
}
}
