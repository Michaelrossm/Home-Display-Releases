#include "FirmwareUpdateOverlay.h"
#include "../services/FirmwareMediaUpdateService.h"
#include "../services/OtaWriteSession.h"
#include "../services/IdleService.h"
#include <lvgl.h>
#include <Arduino.h>
#include <mutex>

namespace hp { namespace {
using Updater=FirmwareMediaUpdateService;
struct Overlay {
 lv_obj_t *panel=nullptr,*heading=nullptr,*versions=nullptr,*message=nullptr,*progress=nullptr,*percent=nullptr,*restart=nullptr,*close=nullptr;
 uint32_t seen=0, dismissed=0;
 bool restartArmed=false;
 Updater::Phase phase=Updater::Phase::Idle;
};
Overlay view;
void restartClicked(lv_event_t*){
 auto& updater=Updater::instance();const auto state=updater.progress();
 if(state.phase!=Updater::Phase::Ready||state.c6||updater.busy())return;
 if(!view.restartArmed){view.restartArmed=true;lv_label_set_text(lv_obj_get_child(view.restart,0),"Neustart bestätigen");return;}
 std::unique_lock<std::mutex> lock(OtaWriteSession::mutex(),std::try_to_lock);
 if(!lock.owns_lock())return;
 ESP.restart();
}
void closeClicked(lv_event_t*){
 auto state=Updater::instance().progress();
 if(state.phase!=Updater::Phase::Failed)return;
 view.dismissed=state.generation;lv_obj_add_flag(view.panel,LV_OBJ_FLAG_HIDDEN);
}
void poll(lv_timer_t*){
 if(!view.panel)return;
 const auto state=Updater::instance().progress();
 if(!state.generation||state.phase==Updater::Phase::Idle||state.generation==view.dismissed){
  lv_obj_add_flag(view.panel,LV_OBJ_FLAG_HIDDEN);return;
 }
 if(state.phase==Updater::Phase::Checking||state.phase==Updater::Phase::Writing||state.phase==Updater::Phase::Verifying||state.phase==Updater::Phase::Rebooting){
  if(state.generation!=view.seen)IdleService::instance().activity();
  IdleService::instance().keepAwake(IdleLease::FirmwareUpdate,5000);
 }
 if(state.generation!=view.seen){
  view.seen=state.generation;view.restartArmed=false;
  lv_label_set_text(lv_obj_get_child(view.restart,0),"Jetzt neu starten");
 }
 lv_obj_remove_flag(view.panel,LV_OBJ_FLAG_HIDDEN);
 view.phase=state.phase;
 const char* title=state.phase==Updater::Phase::Failed?"Update fehlgeschlagen":state.phase==Updater::Phase::Ready?"Update erfolgreich":state.phase==Updater::Phase::Rebooting?"Neustart läuft": "Firmware wird aktualisiert";
 lv_label_set_text(view.heading,title);
 const String versions=String(state.c6?"ESP32-C6":"ESP32-P4")+"   Alt: "+state.previousVersion+"   |   Neu: "+state.nextVersion;
 lv_label_set_text(view.versions,versions.c_str());
 lv_label_set_text(view.message,state.message.c_str());
 lv_bar_set_value(view.progress,state.percent,LV_ANIM_OFF);
 lv_label_set_text_fmt(view.percent,"%u %%",unsigned(state.percent));
 if(state.phase==Updater::Phase::Ready&&!state.c6)lv_obj_remove_flag(view.restart,LV_OBJ_FLAG_HIDDEN);
 else lv_obj_add_flag(view.restart,LV_OBJ_FLAG_HIDDEN);
 if(state.phase==Updater::Phase::Failed)lv_obj_remove_flag(view.close,LV_OBJ_FLAG_HIDDEN);
 else lv_obj_add_flag(view.close,LV_OBJ_FLAG_HIDDEN);
}
void setupLabel(lv_obj_t* label,int size){
 lv_obj_set_style_text_color(label,lv_color_hex(0xf1f5f9),0);
 lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_width(label,LV_PCT(100));
 (void)size;
}
}
void beginFirmwareUpdateOverlay(){
 if(view.panel)return;
 // Top layer is above the desktop, keyboard, saver and other apps.
 view.panel=lv_obj_create(lv_layer_top());
 lv_obj_set_size(view.panel,LV_PCT(100),LV_PCT(100));
 lv_obj_set_pos(view.panel,0,0);
 lv_obj_set_style_radius(view.panel,0,0);
 lv_obj_set_style_pad_all(view.panel,16,0);
 lv_obj_set_style_border_width(view.panel,0,0);
 lv_obj_set_style_bg_color(view.panel,lv_color_hex(0x111827),0);
 lv_obj_set_style_bg_opa(view.panel,LV_OPA_COVER,0);
 lv_obj_add_flag(view.panel,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_clear_flag(view.panel,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_flex_flow(view.panel,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(view.panel,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_set_style_pad_row(view.panel,18,0);
 auto add=[&](const char* text){auto* label=lv_label_create(view.panel);lv_label_set_text(label,text);setupLabel(label,0);return label;};
 auto* brand=add("HOME DISPLAY");
 lv_obj_set_style_text_color(brand,lv_color_hex(0xfb923c),0);
 lv_obj_set_style_text_font(brand,&lv_font_montserrat_24,0);
 view.heading=add("Firmware wird aktualisiert");
 lv_obj_set_style_text_color(view.heading,lv_color_hex(0xfb923c),0);
 lv_obj_set_style_text_font(view.heading,&lv_font_montserrat_28,0);
 view.versions=add("Alt: --   |   Neu: --");
 lv_label_set_long_mode(view.versions,LV_LABEL_LONG_WRAP);
 lv_obj_set_style_text_font(view.versions,&lv_font_montserrat_18,0);
 view.message=add("Update wird vorbereitet ...");
 lv_label_set_long_mode(view.message,LV_LABEL_LONG_WRAP);
 view.progress=lv_bar_create(view.panel);
 lv_obj_set_width(view.progress,LV_PCT(85));lv_obj_set_height(view.progress,18);
 lv_obj_set_style_bg_color(view.progress,lv_color_hex(0x374151),LV_PART_MAIN);
 lv_obj_set_style_bg_color(view.progress,lv_color_hex(0xfb923c),LV_PART_INDICATOR);
 lv_bar_set_range(view.progress,0,100);
 view.percent=add("0 %");
 add("Während der Installation nicht ausschalten oder den Datenträger entfernen.");
 auto button=[&](const char* label,lv_event_cb_t callback){
  auto* b=lv_button_create(view.panel);lv_obj_set_width(b,260);lv_obj_set_height(b,48);
  lv_obj_set_style_bg_color(b,lv_color_hex(0xea580c),0);
  auto* t=lv_label_create(b);lv_label_set_text(t,label);lv_obj_center(t);
  lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,nullptr);
  lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);return b;
 };
 view.restart=button("Jetzt neu starten",restartClicked);
 view.close=button("Fehler schließen",closeClicked);
 lv_obj_add_flag(view.panel,LV_OBJ_FLAG_HIDDEN);
 lv_timer_create(poll,250,nullptr);
}
}
