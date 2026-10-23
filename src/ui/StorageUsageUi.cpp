#include "StorageUsageUi.h"
#include "SettingsWidgets.h"
#include "SecurityUi.h"
#include "../services/StorageService.h"
#include "../services/ConfigService.h"
#include "../services/SecurityService.h"
#include "../services/UsbStorageService.h"
#include "../services/SdFormatConfirmation.h"
#include <new>
#include <cstring>

namespace hp {
namespace {
struct VolumeWidgets { lv_obj_t *amount, *detail, *bar; };
struct Page {
    lv_obj_t *root, *status, *choiceStatus, *firmwareDetail, *apps;
    lv_obj_t* categories[8]{};
    VolumeWidgets volumes[7]{};
    lv_obj_t *usbStatus=nullptr,*usbWarning=nullptr,*usbButton=nullptr,*usbButtonLabel=nullptr,*usbCancel=nullptr;
    SdFormatConfirmation usbConfirmation;
    uint32_t usbGeneration=0;
    bool usbRefreshPending=false;
    StorageUsageSnapshot snapshot;
    uint32_t generation=0;
    lv_timer_t* timer=nullptr;
};
String bytes(uint64_t value) {
    if(value<1024)return String(static_cast<unsigned long>(value))+" B";
    if(value<1024ULL*1024)return String(double(value)/1024,1)+" KiB";
    if(value<1024ULL*1024*1024)return String(double(value)/(1024*1024),2)+" MiB";
    return String(double(value)/(1024ULL*1024*1024),2)+" GiB";
}
void text(lv_obj_t* label,const String& value){
    if(strcmp(lv_label_get_text(label),value.c_str()))lv_label_set_text(label,value.c_str());
}
lv_obj_t* card(lv_obj_t* parent,const char* title,int width=100){
    auto* box=SettingsWidgets::content(parent,false);lv_obj_set_flex_grow(box,0);
    lv_obj_set_width(box,LV_PCT(width));lv_obj_set_height(box,LV_SIZE_CONTENT);
    SettingsWidgets::card(box);lv_obj_set_style_pad_all(box,18,0);lv_obj_set_style_pad_row(box,12,0);
    SettingsWidgets::label(box,title,0,&lv_font_montserrat_18);return box;
}
void update(Page& page){
    auto& storage=StorageService::instance();
    auto& usb=UsbStorageService::instance();
    if(page.usbGeneration!=usb.generation()){
        page.usbGeneration=usb.generation();page.usbConfirmation.reset();page.usbRefreshPending=true;
    }
    if(page.usbRefreshPending&&!usb.formatBusy()&&storage.requestUsageReport())page.usbRefreshPending=false;
    const auto confirmation=page.usbConfirmation.stage(millis());
    if(page.usbStatus){
        text(page.usbStatus,usb.formatStatus());
        text(page.usbButtonLabel,confirmation==1?"1/2: Ja, USB-Daten loeschen":confirmation==2?"2/2: USB endgueltig formatieren":"USB-Speicher formatieren ...");
        text(page.usbWarning,confirmation==1?"Bestaetigung 1 von 2: Alle Dateien im USB-Dateisystem werden geloescht. Interner Speicher und SD bleiben unveraendert.":
             confirmation==2?"Bestaetigung 2 von 2: Wirklich formatieren? Keine Wiederherstellung. USB nicht wechseln/abziehen, Strom nicht unterbrechen!":
             "Manuell als FAT16/FAT32 formatieren. Alle USB-Dateien werden geloescht; zwei weitere Bestaetigungen erforderlich. Kein sicheres Ueberschreiben.");
        if(!usb.present()||usb.formatBusy())lv_obj_add_state(page.usbButton,LV_STATE_DISABLED);
        else lv_obj_remove_state(page.usbButton,LV_STATE_DISABLED);
        if(!confirmation||usb.formatBusy())lv_obj_add_flag(page.usbCancel,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(page.usbCancel,LV_OBJ_FLAG_HIDDEN);
    }
    const bool ready=storage.usageSnapshot(page.snapshot);
    text(page.status,storage.usageBusy()?"Belegung wird im Hintergrund aktualisiert …":ready?
        (page.snapshot.incomplete?"Momentaufnahme · Datei-Analyse teilweise begrenzt":"Momentaufnahme · Analyse abgeschlossen"):"Noch keine Speicheranalyse verfügbar");
    if(!ready||page.generation==page.snapshot.generation)return;
    page.generation=page.snapshot.generation;
    for(size_t i=0;i<7;++i){
        const auto& value=page.snapshot.volumes[i];auto& widgets=page.volumes[i];
        if(!value.available){text(widgets.amount,"Nicht verfügbar");text(widgets.detail,i==6?usb.formatStatus():i==3?"Keine SD-Karte eingesetzt":"Speicher nicht eingebunden");lv_bar_set_value(widgets.bar,0,LV_ANIM_OFF);continue;}
        const uint64_t free=value.total>value.used?value.total-value.used:0;
        text(widgets.amount,bytes(value.used));
        text(widgets.detail,String(i==0?"Firmware von ":"Belegt von ")+bytes(value.total)+"\n"+
            (i==0?String("Rest im OTA-Slot: "):String("Frei: "))+bytes(free));
        const int percent=value.total?int((double(value.used)/double(value.total))*1000):0;
        lv_bar_set_value(widgets.bar,LV_CLAMP(0,percent,1000),LV_ANIM_OFF);
    }
    text(page.firmwareDetail,"Flash gesamt: "+bytes(page.snapshot.flashTotal)+" · OTA-/Rollback-Reserve: "+bytes(page.snapshot.otaReserve)+
        "\nNative Apps gehören zur Firmware. OTA-Reserve ist nicht als App-Speicher nutzbar; RAM/PSRAM sind flüchtig.");
    for(size_t i=0;i<8;++i)text(page.categories[i],bytes(page.snapshot.categories[i]));
    lv_obj_clean(page.apps);
    if(!page.snapshot.appCount)SettingsWidgets::section(page.apps,"Keine Lua-App-Pakete gefunden.");
    for(uint8_t i=0;i<page.snapshot.appCount;++i){
        const auto& app=page.snapshot.apps[i];auto* row=SettingsWidgets::row(page.apps,48);
        auto* name=SettingsWidgets::label(row,app.id,0);lv_obj_set_flex_grow(name,1);lv_obj_set_width(name,LV_PCT(45));lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        auto* place=SettingsWidgets::label(row,app.usb?"USB":app.sd?"SD-Karte":"Intern",100,&lv_font_montserrat_14);
        lv_obj_set_style_text_color(place,app.sd?lv_color_hex(0x6EE7B7):UiTheme::instance().accent(),0);
        auto* size=SettingsWidgets::label(row,bytes(app.bytes).c_str(),130);lv_obj_set_style_text_align(size,LV_TEXT_ALIGN_RIGHT,0);
    }
}
void poll(lv_timer_t* timer){update(*static_cast<Page*>(lv_timer_get_user_data(timer)));}
void usbFormat(lv_event_t* event){
    auto& page=*static_cast<Page*>(lv_event_get_user_data(event));auto& usb=UsbStorageService::instance();
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){
        page.usbConfirmation.reset();SecurityUi::requestSettingsUnlock();update(page);return;
    }
    if(!usb.present()||usb.formatBusy())return;
    const auto now=millis();
    if(page.usbGeneration!=usb.generation()){page.usbConfirmation.reset();page.usbGeneration=usb.generation();}
    if(!page.usbConfirmation.stage(now))page.usbConfirmation.begin(now);
    else if(page.usbConfirmation.confirm(now)&&!usb.requestFormat(page.usbGeneration))text(page.status,"USB-Formatierung nicht gestartet. Bitte erneut pruefen.");
    update(page);
}
void usbCancel(lv_event_t* event){auto& page=*static_cast<Page*>(lv_event_get_user_data(event));page.usbConfirmation.reset();update(page);}
void refresh(lv_event_t* event){
    auto& page=*static_cast<Page*>(lv_event_get_user_data(event));
    text(page.status,StorageService::instance().requestUsageReport()?"Belegung wird im Hintergrund aktualisiert …":"Analyse nicht gestartet. Bitte erneut versuchen.");
}
void choice(lv_event_t* event){
    auto& page=*static_cast<Page*>(lv_event_get_user_data(event));
    auto* target=static_cast<lv_obj_t*>(lv_event_get_target(event));
    const auto selected=lv_dropdown_get_selected(target);
    const bool sd=selected==1,usb=selected==2;
    auto& storage=StorageService::instance();
    SecurityWriteGuard guard;
    const bool allowed=SecurityService::instance().settingsAllowed()&&!SecurityService::instance().tabletLocked();
    const bool ok=allowed&&storage.setNewAppStorage(usb?StorageClass::UsbAppPackage:sd?StorageClass::Media:StorageClass::AppPackage);
    if(!ok){const auto saved=storage.newAppStorage();lv_dropdown_set_selected(target,saved==StorageClass::UsbAppPackage?2:saved==StorageClass::Media?1:0);if(!allowed)SecurityUi::requestSettingsUnlock();}
    text(page.choiceStatus,!ok?"Speicherwahl nicht gespeichert. Einstellungen entsperren oder erneut versuchen.":usb?
        (storage.canStore(StorageClass::UsbAppPackage)?"Neue Lua-Apps werden auf USB gespeichert. Stick nicht abziehen.":"USB gewählt · Installationen benötigen einen eingebundenen FAT16/FAT32-Stick."):sd&&!StorageService::instance().sdAvailable()?
        "SD gewählt · Neue Installationen warten auf eine verfügbare Karte.":sd?"Neue Lua-Apps werden auf SD gespeichert.":"Neue Lua-Apps werden intern gespeichert.");
}
void destroyed(lv_event_t* event){
    auto* page=static_cast<Page*>(lv_event_get_user_data(event));if(lv_event_get_target(event)!=page->root)return;
    if(page->timer)lv_timer_delete(page->timer);delete page;
}
}
void StorageUsageUi::build(lv_obj_t* parent){
    auto* page=new(std::nothrow) Page{};if(!page){SettingsWidgets::section(parent,"Speicherübersicht nicht verfügbar.");return;}
    page->root=SettingsWidgets::content(parent,false);lv_obj_set_flex_grow(page->root,0);lv_obj_set_height(page->root,LV_SIZE_CONTENT);
    auto* head=SettingsWidgets::row(page->root,50);
    page->status=SettingsWidgets::label(head,"Speicheranalyse läuft …",0,&lv_font_montserrat_14);lv_obj_set_flex_grow(page->status,1);
    auto* refreshButton=lv_button_create(head);SettingsWidgets::navButton(refreshButton,false);SettingsWidgets::label(refreshButton,"Aktualisieren",0);lv_obj_center(lv_obj_get_child(refreshButton,0));lv_obj_add_event_cb(refreshButton,refresh,LV_EVENT_CLICKED,page);
    auto* grid=SettingsWidgets::content(page->root,false);lv_obj_set_flex_grow(grid,0);lv_obj_set_height(grid,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_style_pad_column(grid,18,0);
    const char* titles[]={"Firmware","Interne App-Pakete","Systemspeicher","SD-Karte","Interner Arbeitsspeicher","PSRAM","USB-Speicher"};
    for(size_t i=0;i<7;++i){
        auto* box=card(grid,titles[i],48);auto& widgets=page->volumes[i];
        widgets.amount=SettingsWidgets::label(box,"…",0,&lv_font_montserrat_28);
        widgets.bar=lv_bar_create(box);lv_obj_set_width(widgets.bar,LV_PCT(100));lv_obj_set_height(widgets.bar,10);lv_bar_set_range(widgets.bar,0,1000);
        lv_obj_set_style_bg_color(widgets.bar,lv_color_hex(0x24394F),LV_PART_MAIN);lv_obj_set_style_bg_color(widgets.bar,UiTheme::instance().accent(),LV_PART_INDICATOR);
        widgets.detail=SettingsWidgets::label(box,"Analyse läuft",0,&lv_font_montserrat_14);lv_obj_set_width(widgets.detail,LV_PCT(100));
    }
    auto* usbCard=card(page->root,"USB-Speicher verwalten");
    page->usbStatus=SettingsWidgets::label(usbCard,"",0,&lv_font_montserrat_14);
    page->usbWarning=SettingsWidgets::label(usbCard,"",0,&lv_font_montserrat_14);lv_obj_set_width(page->usbWarning,LV_PCT(100));lv_label_set_long_mode(page->usbWarning,LV_LABEL_LONG_WRAP);
    page->usbButton=lv_button_create(usbCard);lv_obj_set_width(page->usbButton,LV_PCT(100));
    page->usbButtonLabel=SettingsWidgets::label(page->usbButton,"USB-Speicher formatieren ...",0);lv_obj_center(page->usbButtonLabel);
    lv_obj_add_event_cb(page->usbButton,usbFormat,LV_EVENT_CLICKED,page);
    page->usbCancel=lv_button_create(usbCard);SettingsWidgets::label(page->usbCancel,"Bestaetigung abbrechen - nichts loeschen",0);lv_obj_add_event_cb(page->usbCancel,usbCancel,LV_EVENT_CLICKED,page);
    auto* detail=card(page->root,"Aufteilung & Reservierungen");
    page->firmwareDetail=SettingsWidgets::label(detail,"",0,&lv_font_montserrat_14);lv_obj_set_width(page->firmwareDetail,LV_PCT(100));lv_label_set_long_mode(page->firmwareDetail,LV_LABEL_LONG_WRAP);
    const char* categoryNames[]={"Konfiguration","Cache","Systemdaten","Wiederherstellung","SD-Nutzerdaten","SD-Medien","SD-Protokolle","SD-Sprachmodelle"};
    for(size_t i=0;i<8;++i){auto* row=SettingsWidgets::row(detail,32);
        auto* name=SettingsWidgets::label(row,categoryNames[i],0,&lv_font_montserrat_14);lv_obj_set_flex_grow(name,1);
        page->categories[i]=SettingsWidgets::label(row,"…",140,&lv_font_montserrat_14);lv_obj_set_style_text_align(page->categories[i],LV_TEXT_ALIGN_RIGHT,0);}
    auto* preference=card(page->root,"Speicherort für neue Apps");
    auto* options=lv_dropdown_create(preference);lv_obj_set_width(options,LV_PCT(100));lv_dropdown_set_options(options,"Interner App-Speicher\nSD-Karte\nUSB-Speicher");
    const auto saved=StorageService::instance().newAppStorage();
    lv_dropdown_set_selected(options,saved==StorageClass::UsbAppPackage?2:saved==StorageClass::Media?1:0);lv_obj_add_event_cb(options,choice,LV_EVENT_VALUE_CHANGED,page);
    page->choiceStatus=SettingsWidgets::label(preference,"Updates bleiben am bisherigen Ort. Keine automatische Verschiebung oder Ausweichspeicherung.",0,&lv_font_montserrat_14);lv_obj_set_width(page->choiceStatus,LV_PCT(100));lv_label_set_long_mode(page->choiceStatus,LV_LABEL_LONG_WRAP);
    auto* appsCard=card(page->root,"Installierte Lua-App-Pakete");
    auto* header=SettingsWidgets::row(appsCard,30);auto* name=SettingsWidgets::label(header,"App",0,&lv_font_montserrat_14);lv_obj_set_flex_grow(name,1);SettingsWidgets::label(header,"Speicherort",100,&lv_font_montserrat_14);SettingsWidgets::label(header,"Dateigröße",130,&lv_font_montserrat_14);
    page->apps=SettingsWidgets::content(appsCard,false);lv_obj_set_flex_grow(page->apps,0);lv_obj_set_height(page->apps,LV_SIZE_CONTENT);lv_obj_set_style_pad_row(page->apps,2,0);
    SettingsWidgets::section(appsCard,"Dateigrößen ohne Dateisystem-Overhead. Native Apps sind bereits in der Firmware enthalten.");
    lv_obj_add_event_cb(page->root,destroyed,LV_EVENT_DELETE,page);page->timer=lv_timer_create(poll,1000,page);
    StorageService::instance().requestUsageReport();update(*page);
}
}
