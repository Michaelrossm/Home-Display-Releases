#include "IntercomApp.h"
#include "../../ui/InputKeyboard.h"
#include "IntercomSettingsApi.h"
#include "../goliath/GoliathService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/SecurityService.h"
#include "../../services/VideoService.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/UiTheme.h"
#include "../../ui/SecurityUi.h"
#include "../../core/PsramArray.h"
#include <cstring>
namespace hp {
namespace {
constexpr const char* legacyId=legacyIntercomId;
bool legacy(const IntercomDevice& d){return !strcmp(d.id,legacyId);}
bool newDevice(const IntercomDevice* devices,size_t count,IntercomDevice& result){
    size_t installed=0;for(size_t i=0;i<count;++i)if(!legacy(devices[i]))++installed;
    if(installed>=4)return false;
    IntercomDevice draft{};
    strlcpy(draft.owner,"intercom",sizeof(draft.owner));
    strlcpy(draft.provider,"dahua",sizeof(draft.provider));
    strlcpy(draft.name,"Neue Sprechanlage",sizeof(draft.name));
    strlcpy(draft.videoPath,"/cam/realmonitor?channel=1&subtype=1",sizeof(draft.videoPath));
    draft.enabled=true;draft.ringTone=false;
    for(unsigned n=1;n<=4;++n){
        snprintf(draft.id,sizeof(draft.id),"intercom-%u",n);bool used=false;
        for(size_t i=0;i<count;++i)used|=!strcmp(devices[i].id,draft.id);
        if(!used){result=draft;return true;}
    }
    return false;
}
bool videoStatusDue(const VideoSnapshot& previous,const VideoSnapshot& next,bool initialized,uint32_t now,uint32_t at){
    return !initialized||uint32_t(now-at)>=1000||previous.generation!=next.generation||
        previous.active!=next.active||previous.connecting!=next.connecting||
        strcmp(previous.codec,next.codec)||strcmp(previous.error,next.error);
}
const char* displayError(const char* code){
    if(!strcmp(code,"pin_locked"))return "Einstellungs-PIN gesperrt. Über PIN freigeben entsperren.";
    if(!strcmp(code,"busy"))return "System beschäftigt. Bitte kurz warten und erneut versuchen.";
    if(!strcmp(code,"config_write_failed"))return "Speichern fehlgeschlagen. Die bisherige Konfiguration wurde beibehalten.";
    if(!strcmp(code,"video_unavailable"))return "Video nicht verfügbar: Aktivierung, PIN und Berechtigungen prüfen.";
    if(!strcmp(code,"host_invalid"))return "Adresse ungültig. Nur IP oder Hostname eingeben, ohne http:// oder Pfad.";
    if(!strcmp(code,"device_limit"))return "Alle vier Geräteplätze sind belegt.";
    if(!strcmp(code,"secret_write_failed"))return "Zugangsdaten konnten nicht sicher gespeichert werden.";
    if(!strcmp(code,"device_invalid")||!strcmp(code,"device_fields"))return "Gerätedaten ungültig. Adresse, Name und Ports prüfen.";
    return code;
}
lv_obj_t* text(lv_obj_t* parent,const char* value){
    auto* label=lv_label_create(parent);lv_label_set_text(label,value);
    lv_obj_set_style_text_font(label,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_set_width(label,LV_PCT(100));return label;
}
}
struct IntercomPage {
    enum class View { Overview, Devices, Live, Settings, Diagnostics };
    View view=View::Overview;
    bool settingsEntry=false,initialLoad=true;
    IntercomApp* app=nullptr;
    lv_obj_t *root=nullptr,*list=nullptr,*detail=nullptr,*status=nullptr,*image=nullptr,*keyboard=nullptr;
    lv_obj_t *fields[9]{},*provider=nullptr,*transport=nullptr,*enabled=nullptr,*ringTone=nullptr,*save=nullptr,*remove=nullptr;
    lv_obj_t *answer=nullptr,*hangup=nullptr,*mute=nullptr,*door=nullptr;
    lv_obj_t *heading=nullptr,*workspace=nullptr,*navigation[5]{};
    lv_timer_t* timer=nullptr;
    IntercomDevice devices[5]{},selected{};
    size_t count=0;
    uint32_t call=0,doorCall=0,doorUntil=0;
    bool editing=false,deleteConfirmed=false,draft=false;
    std::shared_ptr<VideoFrame> frame;
    lv_image_dsc_t descriptor{};
    VideoSnapshot shownVideo{};
    uint32_t statusAt=0;
    bool statusKnown=false;
    static void setDisabled(lv_obj_t* object,bool disabled){
        if(lv_obj_has_state(object,LV_STATE_DISABLED)==disabled)return;
        if(disabled)lv_obj_add_state(object,LV_STATE_DISABLED);else lv_obj_remove_state(object,LV_STATE_DISABLED);
    }
    void activate(View next,const char* title,bool deviceList=false){
        view=next;lv_label_set_text(heading,title);
        for(unsigned i=0;i<5;++i){if(navigation[i]){
            if(i==unsigned(next))lv_obj_add_state(navigation[i],LV_STATE_CHECKED);
            else lv_obj_remove_state(navigation[i],LV_STATE_CHECKED);}}
        if(deviceList){lv_obj_remove_flag(list,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(detail,LV_OBJ_FLAG_HIDDEN);}
        else{lv_obj_add_flag(list,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(detail,LV_OBJ_FLAG_HIDDEN);}
    }
    static void overview(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));p.deviceList(true);}
    static void devicesView(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));p.deviceList(false);}
    void deviceList(bool overviewMode){stop();clearDetail();editing=true;
        activate(overviewMode?View::Overview:View::Devices,overviewMode?"Sprechanlage · Übersicht":"Sprechanlage · Geräte",true);cards();}
    lv_obj_t* button(lv_obj_t* parent,const char* title,void(*fn)(lv_event_t*),void* data=nullptr){
        auto* b=lv_button_create(parent);lv_obj_set_size(b,140,48);UiTheme::instance().stylePrimaryButton(b);
        auto* l=lv_label_create(b);lv_label_set_text(l,title);lv_obj_center(l);
        lv_obj_set_style_text_font(l,fontWithGermanFallback(&lv_font_montserrat_18),0);
        lv_obj_add_event_cb(b,fn,LV_EVENT_CLICKED,data?data:this);return b;
    }
    void hideKeyboard(){lv_keyboard_set_textarea(keyboard,nullptr);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_bottom(root,16,0);}
    void bubblePress(lv_obj_t* object){if(object==keyboard)return;
        lv_obj_add_flag(object,LV_OBJ_FLAG_EVENT_BUBBLE);
        for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)bubblePress(lv_obj_get_child(object,i));}
    static void focus(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        InputKeyboard::bind(p.keyboard,static_cast<lv_obj_t*>(lv_event_get_target(e)));
        lv_obj_set_style_pad_bottom(p.root,296,0);
        lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p.keyboard);
        lv_obj_scroll_to_view(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_ANIM_OFF);}
    static void outsidePress(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(lv_obj_has_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN))return;
        auto* target=static_cast<lv_obj_t*>(lv_event_get_target(e));
        for(auto* o=target;o;o=lv_obj_get_parent(o)){
            if(o==p.keyboard||lv_obj_check_type(o,&lv_textarea_class))return;
        }
        p.hideKeyboard();}
    static void keyboardEvent(lv_event_t* e){if(lv_event_get_code(e)!=LV_EVENT_READY&&lv_event_get_code(e)!=LV_EVENT_CANCEL)return;
        auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        p.hideKeyboard();}
    static void refresh(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(!p.app->queue(IntercomApp::Operation::Load))lv_label_set_text(p.status,"Aktualisieren noch nicht möglich: laufenden Auftrag abwarten.");}
    static void choose(lv_event_t* e,bool configure){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(p.app->busy_.load()){lv_label_set_text(p.status,"Bitte laufenden Auftrag abwarten.");return;}
        const auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if(index<p.count){p.stop();p.draft=false;p.selected=p.devices[index];if(configure||p.view==View::Devices||p.settingsEntry)p.editor();else p.showVideo();}}
    static void select(lv_event_t* e){choose(e,false);}
    static void editDevice(lv_event_t* e){choose(e,true);}
    static void add(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(p.app->busy_.load()){lv_label_set_text(p.status,"Bitte laufenden Auftrag abwarten.");return;}
        if(p.initialLoad){lv_label_set_text(p.status,"Geräteliste noch nicht geladen. Zuerst Aktualisieren drücken.");return;}
        IntercomDevice device;
        if(!newDevice(p.devices,p.count,device)){lv_label_set_text(p.status,"Maximal vier zusätzliche Geräte möglich.");return;}
        p.stop();p.selected=device;p.draft=true;p.editor();
        lv_label_set_text(p.status,"Adresse und Zugangsdaten eingeben. Speichern verbindet nicht automatisch.");}
    void stop(){++app->generation_;VideoService::instance().close("intercom");GoliathService::instance().stopCamera();
        if(image&&lv_obj_is_valid(image))lv_image_set_src(image,nullptr);lv_image_cache_drop(&descriptor);frame.reset();}
    static void start(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(!p.selected.id[0]||p.draft)return;
        if(p.selected.id[0]&&!p.app->queue(IntercomApp::Operation::Start,&p.selected))lv_label_set_text(p.status,"Bitte laufenden Auftrag abwarten.");}
    static void stopEvent(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));p.stop();lv_label_set_text(p.status,"Video gestoppt.");}
    static void saved(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        auto d=p.selected;
        const unsigned capacities[]={sizeof(d.name),sizeof(d.host),sizeof(d.username),193,sizeof(d.participant),6,6,sizeof(d.videoPath),3};
        for(unsigned i=0;i<9;++i)if(strlen(lv_textarea_get_text(p.fields[i]))>=capacities[i]){
            lv_label_set_text(p.status,"Ein Feld ist zu lang (UTF-8-Bytegrenze). Bitte kürzen.");return;}
        const unsigned provider=lv_dropdown_get_selected(p.provider);
        if(provider==4){lv_label_set_text(p.status,"SIP-Client noch nicht implementiert; Speichern nicht möglich.");return;}
        const char* names[]={"dahua","hikvision","doorbird","lan_event"};strlcpy(d.provider,names[provider],sizeof(d.provider));
        strlcpy(d.name,lv_textarea_get_text(p.fields[0]),sizeof(d.name));strlcpy(d.host,lv_textarea_get_text(p.fields[1]),sizeof(d.host));
        strlcpy(d.username,lv_textarea_get_text(p.fields[2]),sizeof(d.username));strlcpy(d.participant,lv_textarea_get_text(p.fields[4]),sizeof(d.participant));
        strlcpy(d.videoPath,lv_textarea_get_text(p.fields[7]),sizeof(d.videoPath));
        if(!d.name[0]||!d.host[0]){lv_label_set_text(p.status,"Name und Adresse fehlen.");return;}
        auto number=[](lv_obj_t* o,unsigned max,unsigned& result){const char* s=lv_textarea_get_text(o);if(!*s)return false;
            unsigned n=0;for(;*s;++s){if(*s<'0'||*s>'9')return false;n=n*10+(*s-'0');if(n>max)return false;}result=n;return n>0;};
        unsigned http,video,fps;
        if(!number(p.fields[5],65535,http)||!number(p.fields[6],65535,video)||!number(p.fields[8],10,fps)){
            lv_label_set_text(p.status,"Ungültiger Port oder FPS (1–10).");return;}
        d.port=http;d.videoPort=video;d.fps=fps;d.enabled=lv_obj_has_state(p.enabled,LV_STATE_CHECKED);
        d.httpVideo=lv_dropdown_get_selected(p.transport)==1;
        d.ringTone=lv_obj_has_state(p.ringTone,LV_STATE_CHECKED);
        if(legacy(d)&&provider!=0){lv_label_set_text(p.status,"Bestandsgerät bleibt Goliath/Dahua. Für andere Hersteller neues Gerät anlegen.");return;}
        if(p.app->queue(IntercomApp::Operation::Save,&d,lv_textarea_get_text(p.fields[3]))){
            lv_textarea_set_text(p.fields[3],"");p.hideKeyboard();lv_label_set_text(p.status,"Speichern …");}
        else lv_label_set_text(p.status,"Speichern noch nicht möglich: laufenden Auftrag abwarten.");
    }
    static void removed(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(p.draft||p.app->busy_.load())return;
        if(!p.deleteConfirmed){p.deleteConfirmed=true;lv_label_set_text(p.status,"Gerät löschen? Erneut Löschen drücken zum Bestätigen.");return;}
        if(p.app->queue(IntercomApp::Operation::Remove,&p.selected))lv_label_set_text(p.status,"Gerät wird gelöscht …");
        p.deleteConfirmed=false;}
    static void command(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        auto* object=static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto action=object==p.answer?IntercomAction::Answer:object==p.mute?IntercomAction::Mute:object==p.door?IntercomAction::Door:IntercomAction::Hangup;
        if(action==IntercomAction::Door&&(p.doorCall!=p.call||int32_t(millis()-p.doorUntil)>=0)){
            p.doorCall=p.call;p.doorUntil=millis()+5000;lv_label_set_text(p.status,"Türöffnung bestätigen: erneut innerhalb 5 Sekunden drücken.");return;}
        IntercomSnapshot state;IntercomService::instance().snapshot(state);
        if(!IntercomService::instance().request("intercom",action,p.call,!state.muted,action==IntercomAction::Door))
            lv_label_set_text(p.status,"Aktion nicht verfügbar oder PIN gesperrt.");
        p.doorCall=0;
    }
    void cards(){lv_obj_clean(list);
        const bool overviewMode=view==View::Overview;
        lv_obj_set_flex_flow(list,overviewMode?LV_FLEX_FLOW_ROW_WRAP:LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
        for(size_t i=0;i<count;++i){auto* card=lv_obj_create(list);UiTheme::instance().styleCard(card);
            lv_obj_set_size(card,LV_PCT(overviewMode?47:100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(card,20,0);lv_obj_set_style_pad_row(card,14,0);
            String title=String(LV_SYMBOL_VIDEO " ")+devices[i].name;auto* heading=text(card,title.c_str());lv_obj_set_style_text_font(heading,fontWithGermanFallback(&lv_font_montserrat_24),0);
            text(card,!strcmp(devices[i].provider,"dahua")?"Goliath / Dahua":!strcmp(devices[i].provider,"hikvision")?"Hikvision":!strcmp(devices[i].provider,"doorbird")?"DoorBird":"LAN-Rufereignisse");
            String description=String(devices[i].host)+" · "+(devices[i].enabled?"Aktiv – Verbindung ungeprüft":"Deaktiviert");text(card,description.c_str());
            text(card,devices[i].participant[0]?(String("Wohnung / Teilnehmer: ")+devices[i].participant).c_str():"Teilnehmer noch nicht eingerichtet");
            auto* b=button(card,overviewMode?"Livebild öffnen":"Konfigurieren",select);lv_obj_set_width(b,LV_PCT(100));lv_obj_set_user_data(b,reinterpret_cast<void*>(i));
            if(overviewMode){auto* edit=button(card,LV_SYMBOL_EDIT " Bearbeiten",editDevice);lv_obj_set_width(edit,LV_PCT(100));lv_obj_set_user_data(edit,reinterpret_cast<void*>(i));}}
        if(!count)text(list,"Noch keine Geräte. Über Geräte → Hinzufügen eine Außenstation einrichten. Bestehende Goliath-Einstellungen werden automatisch angeboten.");
    }
    static void providerChanged(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        const auto index=lv_dropdown_get_selected(p.provider);const char* paths[]={"/cam/realmonitor?channel=1&subtype=1","/Streaming/channels/102","/bha-api/video.cgi","",""};
        lv_textarea_set_text(p.fields[7],paths[index]);lv_textarea_set_text(p.fields[6],index==2?"80":"554");
        lv_dropdown_set_selected(p.transport,index==2?1:0);
        if(index==3)lv_obj_remove_state(p.ringTone,LV_STATE_DISABLED);else lv_obj_add_state(p.ringTone,LV_STATE_DISABLED);
        if(index==4)lv_obj_add_state(p.save,LV_STATE_DISABLED);else lv_obj_remove_state(p.save,LV_STATE_DISABLED);}
    static void dahuaProfile(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(lv_dropdown_get_selected(p.provider)!=0||legacy(p.selected)){
            lv_label_set_text(p.status,"Dahua-Profil nur für ein separates Dahua-Gerät verfügbar.");return;}
        const bool snapshot=lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))!=nullptr;
        lv_dropdown_set_selected(p.transport,snapshot?1:0);
        lv_textarea_set_text(p.fields[6],snapshot?"80":"554");
        lv_textarea_set_text(p.fields[7],snapshot?"/cgi-bin/snapshot.cgi?channel=1":"/cam/realmonitor?channel=1&subtype=1");
        lv_textarea_set_text(p.fields[8],snapshot?"1":"5");
        lv_label_set_text(p.status,"Profil ausgewählt. Erst speichern, dann im Livebild starten. Zugangsdaten bleiben unverändert.");}
    void clearDetail(){if(keyboard)hideKeyboard();
        statusKnown=false;if(timer)lv_timer_set_period(timer,200);
        image=nullptr;lv_obj_clean(detail);}
    void editor(){stop();clearDetail();editing=true;deleteConfirmed=false;
        activate(View::Settings,"Sprechanlage · Geräteeinstellungen");
        text(detail,"Gerät, Verbindung und Video");
        text(detail,draft?"Neues Gerät · erst speichern, danach Livebild starten":"Gespeichertes Gerät · Änderungen werden erst mit Speichern übernommen");
        lv_obj_set_scroll_dir(detail,LV_DIR_VER);
        provider=lv_dropdown_create(detail);lv_dropdown_set_options(provider,"Goliath / Dahua (nur Video)\nHikvision (nur Video)\nDoorBird (nur Video)\nLAN-Rufereignisse\nSIP (noch nicht verfügbar)");
        const unsigned index=!strcmp(selected.provider,"hikvision")?1:!strcmp(selected.provider,"doorbird")?2:!strcmp(selected.provider,"lan_event")?3:0;
        lv_dropdown_set_selected(provider,index);lv_obj_set_width(provider,LV_PCT(100));
        text(detail,"Video-Transport");transport=lv_dropdown_create(detail);
        lv_dropdown_set_options(transport,"RTSP / TCP (H.264 Baseline)\nHTTP (JPEG-Snapshot / MJPEG)");lv_obj_set_width(transport,LV_PCT(100));
        lv_dropdown_set_selected(transport,selected.httpVideo||index==2?1:0);
        const char* labels[]={"Name","IP / Hostname","Benutzername","Passwort (leer = unverändert)","Teilnehmer / Wohnung","HTTP-Port","Video-Port","Stream-Pfad (leer = Herstellerprofil)","Ausgabe-FPS (1–10)"};
        String values[]={selected.name,selected.host,selected.username,"",selected.participant,String(selected.port),String(selected.videoPort),selected.videoPath,String(selected.fps)};
        const unsigned limits[]={64,96,64,192,64,5,5,192,2};
        auto* form=SettingsWidgets::content(detail,false);lv_obj_set_width(form,LV_PCT(100));lv_obj_set_height(form,LV_SIZE_CONTENT);lv_obj_set_flex_grow(form,0);
        lv_obj_set_flex_flow(form,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_style_pad_column(form,16,0);
        for(unsigned i=0;i<9;++i){
            auto* cell=SettingsWidgets::content(form,false);lv_obj_set_width(cell,LV_PCT(i==7?100:48));lv_obj_set_height(cell,LV_SIZE_CONTENT);lv_obj_set_flex_grow(cell,0);
            text(cell,labels[i]);fields[i]=lv_textarea_create(cell);
            lv_textarea_set_one_line(fields[i],true);lv_textarea_set_max_length(fields[i],limits[i]);
            lv_obj_set_width(fields[i],LV_PCT(100));lv_textarea_set_text(fields[i],values[i].c_str());
            SettingsWidgets::darkInput(fields[i]);
            if(i==3)lv_textarea_set_password_mode(fields[i],true);
            if(i==1)lv_textarea_set_placeholder_text(fields[i],"z. B. 192.168.178.110 oder tuer.local");
            if(i==4)lv_textarea_set_placeholder_text(fields[i],"Eigene Wohnung / Teilnehmer (optional für Video)");
            if(i==5||i==6||i==8)lv_textarea_set_accepted_chars(fields[i],"0123456789");
            lv_obj_add_event_cb(fields[i],focus,LV_EVENT_FOCUSED,this);
            lv_obj_add_event_cb(fields[i],focus,LV_EVENT_CLICKED,this);}
        enabled=lv_switch_create(detail);if(selected.enabled)lv_obj_add_state(enabled,LV_STATE_CHECKED);text(detail,"Gerät aktivieren");
        text(detail,"Klingelton für bestätigte Rufereignisse");ringTone=lv_switch_create(detail);
        if(selected.ringTone)lv_obj_add_state(ringTone,LV_STATE_CHECKED);
        if(strcmp(selected.provider,"lan_event"))lv_obj_add_state(ringTone,LV_STATE_DISABLED);
        auto* row=SettingsWidgets::row(detail,58);save=button(row,"Speichern",saved);remove=button(row,"Löschen",removed);
        button(row,"PIN freigeben",[](lv_event_t*){SecurityUi::requestSettingsUnlock();});
        if(draft)lv_obj_add_state(remove,LV_STATE_DISABLED);
        if(legacy(selected)){lv_obj_add_state(remove,LV_STATE_DISABLED);lv_obj_add_state(provider,LV_STATE_DISABLED);
            lv_obj_add_state(transport,LV_STATE_DISABLED);
            for(unsigned i:{0u,7u,8u})lv_obj_add_state(fields[i],LV_STATE_DISABLED);
            text(detail,"Bestandsprofil nutzt die vorhandenen Goliath-Einstellungen und Secrets. Name, Streamprofil, FPS und Löschen hier gesperrt.");}
        else lv_obj_add_event_cb(provider,providerChanged,LV_EVENT_VALUE_CHANGED,this);
        if(index==0&&!legacy(selected)){
            text(detail,"VTO2000A: RTSP Basic / H.264 Baseline, 25 FPS Quelle; HTTP-JPEG an der vorhandenen Station geprüft. Klingelrouting und Gegensprechen noch offen.");
            auto* profiles=SettingsWidgets::row(detail,58);
            button(profiles,"Live-Profil",dahuaProfile);
            auto* snapshot=button(profiles,"JPEG-Profil",dahuaProfile);lv_obj_set_user_data(snapshot,reinterpret_cast<void*>(1));
        }
        text(detail,"Klingeln, Gegensprechen und Türöffnung sind bei diesen Kamera-Adaptern nicht implementiert. Erreichbarkeit bedeutet keine vollständige Kompatibilität.");
        bubblePress(root);
    }
    void showVideo(){stop();clearDetail();editing=false;
        activate(View::Live,"Sprechanlage · Livebild");
        if(!selected.id[0]||draft){text(detail,draft?"Neues Gerät zuerst unter Einstellungen speichern. Danach hier das Livebild starten.":"Zuerst unter Übersicht eine Außenstation auswählen.");return;}
        if(timer&&selected.fps>=1&&selected.fps<=10)lv_timer_set_period(timer,1000/selected.fps);
        text(detail,selected.name);image=lv_image_create(detail);lv_obj_set_size(image,LV_PCT(100),0);lv_obj_set_flex_grow(image,1);
        lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);
        auto* row=SettingsWidgets::row(detail,58);auto* b=button(row,"Start",start);button(row,"Stopp",stopEvent);
        if(!selected.enabled||!strcmp(selected.provider,"lan_event")){lv_obj_add_state(b,LV_STATE_DISABLED);text(detail,"Video nicht verfügbar: Gerät deaktiviert oder kein Video-Adapter.");}
    }
    static void deleted(lv_event_t* e){auto* p=static_cast<IntercomPage*>(lv_event_get_user_data(e));
        if(p->timer)lv_timer_delete(p->timer);p->stop();p->app->page_=nullptr;++p->app->generation_;
        p->~IntercomPage();heap_caps_free(p);}
    static void live(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));p.showVideo();}
    static void settings(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));if(p.selected.id[0])p.editor();else p.deviceList(false);}
    static void diagnose(lv_event_t* e){auto& p=*static_cast<IntercomPage*>(lv_event_get_user_data(e));p.app->queue(IntercomApp::Operation::Diagnose);}
    static void tick(lv_timer_t* t){auto& p=*static_cast<IntercomPage*>(lv_timer_get_user_data(t));auto& app=*p.app;
        if(app.done_.exchange(false,std::memory_order_acquire)){
            auto& w=app.work_;
            const bool current=w.generation==app.generation_.load();
            if(current){
                if(w.operation==IntercomApp::Operation::Load){p.count=w.count;for(size_t i=0;i<p.count;++i)p.devices[i]=w.devices[i];p.cards();
                    if(p.initialLoad){p.initialLoad=false;if(p.count)p.selected=p.devices[0];if(p.settingsEntry&&p.count)p.editor();}}
                if(w.operation==IntercomApp::Operation::Diagnose){p.stop();p.clearDetail();p.editing=true;p.activate(View::Diagnostics,"Sprechanlage · Diagnose");text(p.detail,w.diagnostics);}
                lv_label_set_text(p.status,w.ok?"Auftrag abgeschlossen.":displayError(w.error));
                if(w.ok&&w.operation==IntercomApp::Operation::Save){p.selected=w.device;p.draft=false;p.deviceList(false);}
            }
            app.busy_=false;
            if(current&&w.ok&&w.operation==IntercomApp::Operation::Remove){p.selected={};p.draft=false;p.deviceList(false);}
            if(current&&w.ok&&(w.operation==IntercomApp::Operation::Save||w.operation==IntercomApp::Operation::Remove))app.queue(IntercomApp::Operation::Load);
        }
        IntercomSnapshot s;if(IntercomService::instance().snapshot(s)){
            p.call=s.call;const bool owns=!strcmp(s.device,p.selected.id),locked=SecurityService::instance().tabletLocked();
            setDisabled(p.answer,locked||!owns||!s.capabilities.answer||s.state!=IntercomState::Ringing);
            setDisabled(p.mute,locked||!owns||!s.capabilities.duplex||s.state!=IntercomState::Conversation);
            setDisabled(p.door,locked||!owns||!s.capabilities.door||s.state!=IntercomState::Conversation);
            setDisabled(p.hangup,!owns||(s.state!=IntercomState::Ringing&&s.state!=IntercomState::Conversation&&s.state!=IntercomState::Connecting));
        }else{
            for(auto* b:{p.answer,p.hangup,p.mute,p.door})setDisabled(b,true);
        }
        if(!p.image||p.editing)return;
        VideoSnapshot video;const bool known=legacy(p.selected)?GoliathService::instance().cameraSnapshot(video):VideoService::instance().snapshot("intercom",video);
        if(known&&videoStatusDue(p.shownVideo,video,p.statusKnown,millis(),p.statusAt)){
            p.statusAt=millis();p.statusKnown=true;p.shownVideo=video;
            String message=String(video.connecting?"Verbindung … ":video.active?"Livebild ":"Gestoppt ")+video.codec+" · FPS: "+String(video.fpsMilli/1000.0f,1)+" · Frames: "+video.frames+" · Fehler: "+video.errors+" · "+video.error;
            if(strcmp(lv_label_get_text(p.status),message.c_str()))lv_label_set_text(p.status,message.c_str());}
        auto next=legacy(p.selected)?GoliathService::instance().cameraFrame():VideoService::instance().frame("intercom");
        if(next==p.frame)return;
        lv_image_set_src(p.image,nullptr);lv_image_cache_drop(&p.descriptor);p.frame=next;
        if(!next)return;const auto& decoded=*next->image;p.descriptor={};
        p.descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;p.descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
        p.descriptor.header.w=decoded.width;p.descriptor.header.h=decoded.height;p.descriptor.header.stride=decoded.width*2;
        p.descriptor.data_size=decoded.width*decoded.height*2;p.descriptor.data=decoded.pixels;
        lv_image_set_src(p.image,&p.descriptor);
    }
};
bool IntercomApp::queue(Operation op,const IntercomDevice* device,const char* password){
    if(done_.load()&&!page_){done_=false;busy_=false;}
    bool expected=false;if(!busy_.compare_exchange_strong(expected,true))return false;
    // Do not materialize the complete multi-profile result on the LVGL task stack.
    work_.operation=op;work_.count=0;work_.ok=false;work_.error[0]=0;work_.diagnostics[0]=0;
    work_.password[0]=0;work_.generation=generation_.load();if(device)work_.device=*device;
    if(password)strlcpy(work_.password,password,sizeof(work_.password));
    if(NetworkWorker::instance().submitPriority(worker,this,"intercom-ui"))return true;
    memset(work_.password,0,sizeof(work_.password));busy_=false;return false;
}
void IntercomApp::worker(void* context){auto& app=*static_cast<IntercomApp*>(context);auto& w=app.work_;String error;
    if(w.generation!=app.generation_.load()){strlcpy(w.error,"Auftrag verworfen.",sizeof(w.error));}
    else if(w.operation==Operation::Load){
        w.count=nativeIntercomDevices(w.devices,5);
        w.ok=true;
    }else if(w.operation==Operation::Diagnose){
        const String diagnostics=IntercomService::instance().diagnostics();
        snprintf(w.diagnostics,sizeof(w.diagnostics),"RAM intern frei: %u Byte\nGrößter interner Block: %u Byte\nPSRAM frei: %u Byte\n\nGoliath: %s\nEreignisse (nur Diagnose): %u\n\n%s",
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),GoliathService::instance().eventStatus().c_str(),
            unsigned(GoliathService::instance().observedCallEvents()),diagnostics.c_str());w.ok=true;
    }else if(w.operation==Operation::Start){
        w.ok=startNativeIntercomVideo(w.device,error);
        if(w.generation!=app.generation_.load()){VideoService::instance().close("intercom");GoliathService::instance().stopCamera();w.ok=false;error="Auftrag verworfen.";}
    }else if(w.operation==Operation::Remove)w.ok=removeNativeIntercomDevice(w.device.id,error);
    else {
        String password=w.password;w.ok=saveNativeIntercomDevice(w.device,w.password[0]?&password:nullptr,error);
    }
    memset(w.password,0,sizeof(w.password));if(error.length())strlcpy(w.error,error.c_str(),sizeof(w.error));
    app.done_.store(true,std::memory_order_release);
}
void IntercomApp::create(lv_obj_t* parent,bool showSettings){close();++generation_;
    page_=allocatePsramArray<IntercomPage>(1);if(!page_){text(parent,"Nicht genug Speicher für Sprechanlage.");return;}
    auto& p=*page_;p.app=this;p.settingsEntry=showSettings;p.root=SettingsWidgets::content(parent,false);lv_obj_set_size(p.root,LV_PCT(100),LV_PCT(100));
    UiTheme::instance().styleScreen(p.root);lv_obj_set_style_bg_opa(p.root,LV_OPA_COVER,0);
    lv_obj_set_style_text_font(p.root,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_add_event_cb(p.root,IntercomPage::deleted,LV_EVENT_DELETE,&p);
    lv_obj_set_style_pad_all(p.root,16,0);
    auto* top=SettingsWidgets::row(p.root,62);UiTheme::instance().styleTopBar(top);
    p.heading=text(top,"Sprechanlage · Übersicht");lv_obj_set_width(p.heading,0);lv_obj_set_flex_grow(p.heading,1);
    p.button(top,LV_SYMBOL_HOME " Übersicht",IntercomPage::overview);
    auto* body=SettingsWidgets::row(p.root,0);lv_obj_set_flex_grow(body,1);
    lv_obj_set_flex_align(body,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    p.workspace=SettingsWidgets::content(body,false);lv_obj_set_width(p.workspace,0);lv_obj_set_height(p.workspace,LV_PCT(100));
    p.list=SettingsWidgets::content(p.workspace,true);lv_obj_set_width(p.list,LV_PCT(100));lv_obj_set_height(p.list,0);
    p.detail=SettingsWidgets::content(p.workspace,true);lv_obj_set_width(p.detail,LV_PCT(100));lv_obj_set_height(p.detail,0);lv_obj_add_flag(p.detail,LV_OBJ_FLAG_HIDDEN);
    auto* side=SettingsWidgets::content(body,true);lv_obj_set_width(side,250);lv_obj_set_height(side,LV_PCT(100));lv_obj_set_flex_grow(side,0);
    UiTheme::instance().styleCard(side);lv_obj_set_style_pad_all(side,14,0);lv_obj_set_style_pad_row(side,12,0);
    const char* titles[]={LV_SYMBOL_HOME " Übersicht",LV_SYMBOL_LIST " Geräte",LV_SYMBOL_VIDEO " Livebild",LV_SYMBOL_SETTINGS " Einstellungen",LV_SYMBOL_EYE_OPEN " Diagnose"};
    lv_event_cb_t handlers[]={IntercomPage::overview,IntercomPage::devicesView,IntercomPage::live,IntercomPage::settings,IntercomPage::diagnose};
    for(unsigned i=0;i<5;++i){auto* b=p.button(side,titles[i],handlers[i]);p.navigation[i]=b;lv_obj_set_size(b,LV_PCT(100),54);
        lv_obj_set_style_bg_color(b,UiTheme::instance().surface(),0);lv_obj_set_style_text_color(b,UiTheme::instance().textPrimary(),0);
        lv_obj_set_style_bg_color(b,UiTheme::instance().accent(),LV_STATE_CHECKED);lv_obj_set_style_text_color(b,UiTheme::instance().onAccent(),LV_STATE_CHECKED);}
    auto* hint=text(side,"Video startet nur auf Anforderung. Nicht unterstützte Funktionen bleiben gesperrt.");lv_obj_set_style_text_color(hint,UiTheme::instance().textSecondary(),0);
    auto* addButton=p.button(side,LV_SYMBOL_PLUS " Hinzufügen",IntercomPage::add);lv_obj_set_width(addButton,LV_PCT(100));
    auto* refreshButton=p.button(side,LV_SYMBOL_REFRESH " Aktualisieren",IntercomPage::refresh);lv_obj_set_width(refreshButton,LV_PCT(100));
    auto* commands=SettingsWidgets::row(p.root,58);p.answer=p.button(commands,LV_SYMBOL_CALL " Annehmen",IntercomPage::command);
    p.hangup=p.button(commands,"Ruf schließen",IntercomPage::command);p.mute=p.button(commands,LV_SYMBOL_MUTE " Stumm",IntercomPage::command);p.door=p.button(commands,"Tür öffnen",IntercomPage::command);
    for(auto* b:{p.answer,p.hangup,p.mute,p.door}){lv_obj_set_width(b,180);lv_obj_add_state(b,LV_STATE_DISABLED);}
    p.status=text(p.root,"Geräte werden geladen …");lv_obj_set_height(p.status,52);lv_obj_set_style_text_color(p.status,UiTheme::instance().textSecondary(),0);
    p.activate(showSettings?IntercomPage::View::Devices:IntercomPage::View::Overview,showSettings?"Sprechanlage · Geräte":"Sprechanlage · Übersicht",true);
    p.keyboard=lv_keyboard_create(p.root);lv_obj_set_size(p.keyboard,LV_PCT(100),280);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_FLOATING);
    lv_obj_align(p.keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p.keyboard,IntercomPage::keyboardEvent,LV_EVENT_ALL,&p);
    lv_obj_add_event_cb(p.root,IntercomPage::outsidePress,LV_EVENT_PRESSED,&p);p.bubblePress(p.root);
    p.timer=lv_timer_create(IntercomPage::tick,200,&p);
    if(!queue(Operation::Load))lv_label_set_text(p.status,"Geräteliste konnte nicht geladen werden. Bitte Aktualisieren drücken.");
}
void IntercomApp::open(lv_obj_t* parent){create(parent,false);}
bool IntercomApp::openSettings(lv_obj_t* parent){create(parent,true);return page_!=nullptr;}
void IntercomApp::close(){++generation_;VideoService::instance().close("intercom");GoliathService::instance().stopCamera();
    IntercomService::instance().detachOwner("intercom");if(page_&&page_->root)lv_obj_delete(page_->root);}
}
