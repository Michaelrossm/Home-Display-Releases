#include "SecurityUi.h"
#include "../services/SecurityService.h"
#include "../services/IdleService.h"
#include "UiFonts.h"
#include <atomic>
namespace hp {
namespace {
lv_obj_t *overlay=nullptr,*message=nullptr,*fields[3]{};
lv_obj_t* pinStatus[2]{};
unsigned selected=0,operation=0;PinKind kind=PinKind::Settings;bool tabletDialog=false;
uint32_t lastSecond=UINT32_MAX;std::atomic<bool> unlockRequested{false};
void closeDialog(){if(overlay)lv_obj_delete(overlay);overlay=message=nullptr;for(auto& f:fields)f=nullptr;tabletDialog=false;}
lv_obj_t* label(lv_obj_t* p,const char* text){auto* o=lv_label_create(p);lv_label_set_text(o,text);lv_obj_set_style_text_font(o,fontWithGermanFallback(&lv_font_montserrat_18),0);return o;}
lv_obj_t* button(lv_obj_t* p,const char* text,lv_event_cb_t cb,uintptr_t data=0){auto* b=lv_button_create(p);lv_obj_set_size(b,210,48);label(b,text);lv_obj_center(lv_obj_get_child(b,0));lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(data));return b;}
void submit(lv_event_t*){
    auto& s=SecurityService::instance();bool ok=false;
    const String first=lv_textarea_get_text(fields[0]);
    if(operation==1){String pin=lv_textarea_get_text(fields[1]),repeat=lv_textarea_get_text(fields[2]);
        if(!SecurityService::validPin(pin)){lv_label_set_text(message,"PIN braucht 4 bis 12 Ziffern.");return;}
        if(pin!=repeat){lv_label_set_text(message,"PINs stimmen nicht ueberein.");return;}
        ok=s.setPin(kind,first,pin,repeat);
    }else if(operation==2)ok=s.removePin(kind,first);
    else ok=s.unlock(kind,first);
    // Clear every submitted secret, also on failure.
    for(auto* f:fields)if(f)lv_textarea_set_text(f,"");
    if(ok){closeDialog();return;}
    const uint32_t seconds=s.blockedSeconds(kind);
    if(seconds)lv_label_set_text_fmt(message,"Zu viele Fehlversuche. %lu Sekunden warten.",static_cast<unsigned long>(seconds));
    else lv_label_set_text(message,"Falsche PIN oder Speichern fehlgeschlagen.");
}
void key(lv_event_t* e){IdleService::instance().activity();uintptr_t k=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));if(!fields[selected])return;
    if(k==10)lv_textarea_delete_char(fields[selected]);else {char text[2]={static_cast<char>('0'+k),0};lv_textarea_add_text(fields[selected],text);}}
void dialog(PinKind type,unsigned op,bool tablet=false){
    closeDialog();kind=type;operation=op;tabletDialog=tablet;selected=0;lastSecond=UINT32_MAX;
    overlay=lv_obj_create(lv_layer_sys());lv_obj_set_size(overlay,LV_PCT(100),LV_PCT(100));lv_obj_set_pos(overlay,0,0);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x101B2B),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(overlay,lv_color_hex(0xFFFFFF),0);lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    auto* title=label(overlay,tablet?"Home Display - Geraet gesperrt":type==PinKind::Settings?"Einstellungs-PIN":"Tablet-PIN");lv_obj_align(title,LV_ALIGN_TOP_MID,0,10);
    unsigned n=op==1?3:1;
    for(unsigned i=0;i<n;++i){auto* f=fields[i]=lv_textarea_create(overlay);lv_obj_set_size(f,360,52);lv_obj_align(f,LV_ALIGN_TOP_MID,0,55+i*62);
        lv_textarea_set_one_line(f,true);lv_textarea_set_password_mode(f,true);lv_textarea_set_password_show_time(f,0);lv_textarea_set_max_length(f,12);lv_textarea_set_accepted_chars(f,"0123456789");
        lv_textarea_set_placeholder_text(f,op==1?(i==0?"Aktuelle PIN (falls gesetzt)":i==1?"Neue PIN":"Neue PIN wiederholen"):"PIN");
        lv_obj_add_event_cb(f,[](lv_event_t* e){selected=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));},LV_EVENT_CLICKED,reinterpret_cast<void*>(i));}
    if(op==1&&!SecurityService::instance().enabled(type)){lv_obj_add_state(fields[0],LV_STATE_DISABLED);selected=1;}
    message=label(overlay,"");lv_obj_align(message,LV_ALIGN_TOP_MID,0,250);
    const char* keys[]={"1","2","3","4","5","6","7","8","9","Loeschen","0","OK"};
    for(unsigned i=0;i<12;++i){auto* b=button(overlay,keys[i],i==11?submit:key,i==9?10:i==10?0:i+1);lv_obj_set_size(b,150,65);lv_obj_align(b,LV_ALIGN_TOP_MID,(static_cast<int>(i%3)-1)*160,300+i/3*76);}
    if(!tablet){auto* cancel=button(overlay,"Abbrechen",[](lv_event_t*){closeDialog();});lv_obj_align(cancel,LV_ALIGN_BOTTOM_MID,0,-12);}
}
void manage(lv_event_t* e){const uintptr_t data=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));dialog(data>=10?PinKind::Tablet:PinKind::Settings,data%10);}
}
void SecurityUi::requestSettingsUnlock(){unlockRequested.store(true);}
void SecurityUi::sync(){auto& s=SecurityService::instance();
    for(unsigned i=0;i<2;++i)if(pinStatus[i]){const char* text=s.enabled(i?PinKind::Tablet:PinKind::Settings)?"Aktiv":"Nicht gesetzt";if(strcmp(lv_label_get_text(pinStatus[i]),text))lv_label_set_text(pinStatus[i],text);}
    if(s.tabletLocked()){unlockRequested.store(false);if(!overlay||!tabletDialog)dialog(PinKind::Tablet,0,true);lv_obj_move_foreground(overlay);}
    else if(tabletDialog)closeDialog();
    else if(unlockRequested.exchange(false))dialog(PinKind::Settings,0);
    if(overlay&&lastSecond!=millis()/1000){lastSecond=millis()/1000;const uint32_t seconds=s.blockedSeconds(kind);if(seconds)lv_label_set_text_fmt(message,"Zu viele Fehlversuche. %lu Sekunden warten.",static_cast<unsigned long>(seconds));else if(operation==0)lv_label_set_text(message,"PIN eingeben");}
}
void SecurityUi::buildPage(lv_obj_t* parent){auto& s=SecurityService::instance();
    for(unsigned i=0;i<2;++i){label(parent,i?"Tablet-PIN":"Einstellungs-PIN");pinStatus[i]=label(parent,s.enabled(i?PinKind::Tablet:PinKind::Settings)?"Aktiv":"Nicht gesetzt");
        lv_obj_add_event_cb(pinStatus[i],[](lv_event_t* e){pinStatus[reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))]=nullptr;},LV_EVENT_DELETE,reinterpret_cast<void*>(i));
        button(parent,"PIN setzen / aendern",manage,i*10+1);button(parent,"PIN entfernen",manage,i*10+2);}
    button(parent,"Geraet jetzt sperren",[](lv_event_t*){SecurityService::instance().lockTablet();sync();});
    button(parent,"Einstellungen entsperren",[](lv_event_t*){dialog(PinKind::Settings,0);});
    label(parent,"Nach Display-Aus automatisch sperren");auto* sw=lv_switch_create(parent);if(s.lockAfterDisplayOff())lv_obj_add_state(sw,LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw,[](lv_event_t* e){auto* o=static_cast<lv_obj_t*>(lv_event_get_target(e));bool value=lv_obj_has_state(o,LV_STATE_CHECKED);if(!SecurityService::instance().setLockAfterDisplayOff(value)){if(!value)lv_obj_add_state(o,LV_STATE_CHECKED);else lv_obj_remove_state(o,LV_STATE_CHECKED);requestSettingsUnlock();}},LV_EVENT_VALUE_CHANGED,nullptr);
}
}
