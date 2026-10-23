#include "EmailAccountsUi.h"
#include "../services/SettingsService.h"
#include "SettingsWidgets.h"
#include "QrDialog.h"
#include "SecurityUi.h"
#include "AccountDataUi.h"
#include "../services/QrGenerator.h"
#include "../services/EmailAccountService.h"
#include "../services/SecurityService.h"
#include "../services/CalendarSyncService.h"
#include "../services/CalDavDiscovery.h"
#include "../services/NetworkService.h"
#include <mbedtls/platform_util.h>
#include <cstring>
#include <new>

namespace hp {
namespace {
struct Form {
    lv_obj_t *root, *address, *password, *provider, *hint, *status, *deleteLabel;
    lv_obj_t *oauth, *clientId, *redirect, *clientSecret, *oauthStatus, *purpose, *passwordLabel, *redirectLabel, *secretLabel, *davServer, *davServerLabel, *davTest;
    lv_timer_t* timer = nullptr;
    lv_obj_t* slots[5]{};
    lv_obj_t* phonebook=nullptr;
    lv_obj_t* loginQr=nullptr;lv_obj_t* qrGuide=nullptr;String qrUrl;
    lv_obj_t* data=nullptr;lv_event_cb_t focus=nullptr;void* focusContext=nullptr;
    uint8_t selected = 0;
    uint32_t deleteArmedAt = 0;
};
void eraseInput(lv_obj_t* input, bool deleting = false);
OAuthPurpose purpose(Form& form){return lv_dropdown_get_selected(form.purpose)==0?OAuthPurpose::Identity:OAuthPurpose::Calendar;}
void refreshOAuth(Form& form) {
    const auto provider=EmailProvider(lv_dropdown_get_selected(form.provider));
    const bool supported=provider==EmailProvider::Google||provider==EmailProvider::Microsoft;
    if(supported)lv_obj_remove_flag(form.oauth,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(form.oauth,LV_OBJ_FLAG_HIDDEN);
    eraseInput(form.clientSecret);
    if(supported){eraseInput(form.password);lv_obj_add_flag(form.password,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(form.passwordLabel,LV_OBJ_FLAG_HIDDEN);}
    else{lv_obj_remove_flag(form.password,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(form.passwordLabel,LV_OBJ_FLAG_HIDDEN);}
    const bool dav=provider==EmailProvider::Other;
    for(auto* obj:{form.davServer,form.davServerLabel})if(dav)lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
    if(dav||provider==EmailProvider::Apple)lv_obj_remove_flag(form.davTest,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(form.davTest,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(form.passwordLabel,provider==EmailProvider::Apple?"Nur Apple App-Passwort (xxxx-xxxx-xxxx-xxxx)":"Passwort / App-Passwort");
    const auto setup=CalendarSyncService::instance().setup(provider,purpose(form));
    lv_textarea_set_text(form.clientId,setup.clientId.c_str());lv_textarea_set_text(form.redirect,setup.redirect.c_str());
    lv_textarea_set_placeholder_text(form.clientSecret,setup.secretStored?"Leer = vorhandenes Client-Secret behalten":"Google Client-Secret (nur am Display)");
    for(auto* input:{form.redirect,form.clientSecret}){
        if(provider==EmailProvider::Google)lv_obj_remove_flag(input,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(input,LV_OBJ_FLAG_HIDDEN);
    }
    if(provider==EmailProvider::Google&&purpose(form)==OAuthPurpose::Identity)lv_obj_add_flag(form.redirect,LV_OBJ_FLAG_HIDDEN);
    if(provider==EmailProvider::Google&&purpose(form)==OAuthPurpose::Calendar)lv_obj_remove_flag(form.redirectLabel,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(form.redirectLabel,LV_OBJ_FLAG_HIDDEN);
    if(provider==EmailProvider::Google)lv_obj_remove_flag(form.secretLabel,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(form.secretLabel,LV_OBJ_FLAG_HIDDEN);
}
void eraseInput(lv_obj_t* input, bool deleting) {
    if (!input) return;
    // Password textareas own a writable temporary buffer. Wipe it before
    // LVGL frees/replaces it; never preload a saved secret into the field.
    const char* value = lv_textarea_get_text(input);
    if (value) mbedtls_platform_zeroize(const_cast<char*>(value), strlen(value));
    if (!deleting) lv_textarea_set_text(input, "");
}
void refreshSlots(Form& form) {
    for (uint8_t slot = 0; slot < 5; ++slot) {
        auto info = EmailAccountService::instance().account(slot);
        const auto cached=CalendarSyncService::instance().info(slot);if(CalendarSyncService::instance().calDavServer(slot).length()){info.address=cached.email;info.configured=cached.configured;}
        const String text = String("Konto ") + (slot + 1) + "\n" +
            (info.configured ? info.address : String("Noch nicht eingerichtet"));
        lv_label_set_text(lv_obj_get_child(form.slots[slot], 0), text.c_str());
        SettingsWidgets::navButton(form.slots[slot], slot == form.selected);
    }
}
void select(Form& form, uint8_t slot) {
    if(form.data)lv_obj_clean(form.data);
    eraseInput(form.password); form.selected = slot; form.deleteArmedAt = 0;form.qrUrl="";if(form.loginQr)lv_obj_add_flag(form.loginQr,LV_OBJ_FLAG_HIDDEN);
    auto info = EmailAccountService::instance().account(slot);
    const auto cached=CalendarSyncService::instance().info(slot);const auto server=CalendarSyncService::instance().calDavServer(slot);
    if(server.length()){info.address=cached.email;info.provider=cached.provider;info.configured=cached.configured;}
    lv_textarea_set_text(form.davServer,server.c_str());
    lv_textarea_set_text(form.address, info.address.c_str());
    lv_textarea_set_placeholder_text(form.password, info.configured ? "Leer = vorhandenes Passwort behalten" : "Passwort / App-Passwort eingeben");
    lv_dropdown_set_selected(form.provider, uint8_t(info.provider));
    lv_label_set_text(form.hint, EmailAccountService::providerHint(info.provider));
    lv_label_set_text(form.status, info.configured ? "Verschlüsselt gespeichert · Verbindung noch nicht geprüft" : "Freier Kontoplatz");
    lv_label_set_text(form.deleteLabel, "Konto entfernen"); refreshSlots(form);refreshOAuth(form);
    const auto account=CalendarSyncService::instance().accountInfo(String("account-")+slot);
    if(account.phonebookEnabled)lv_obj_add_state(form.phonebook,LV_STATE_CHECKED);else lv_obj_remove_state(form.phonebook,LV_STATE_CHECKED);
    if(account.id.length())lv_obj_remove_state(form.phonebook,LV_STATE_DISABLED);else lv_obj_add_state(form.phonebook,LV_STATE_DISABLED);
}
void phonebookChanged(lv_event_t* event){auto& form=*static_cast<Form*>(lv_event_get_user_data(event));
    const bool enabled=lv_obj_has_state(form.phonebook,LV_STATE_CHECKED);
    if(!CalendarSyncService::instance().setPhonebookEnabled(String("account-")+form.selected,enabled)){
        if(enabled)lv_obj_remove_state(form.phonebook,LV_STATE_CHECKED);else lv_obj_add_state(form.phonebook,LV_STATE_CHECKED);
        lv_label_set_text(form.status,"Telefonbuch-Option konnte nicht gespeichert werden.");
    }else lv_label_set_text(form.status,"Telefonbuch-Option wird gespeichert. Danach Kontakte ausdrücklich verbinden / freigeben.");
}
void openData(lv_event_t* event){auto& form=*static_cast<Form*>(lv_event_get_user_data(event));lv_obj_clean(form.data);
    AccountDataUi::build(form.data,String("account-")+form.selected,form.focus,form.focusContext);lv_obj_scroll_to_view(form.data,LV_ANIM_OFF);}
void contactAction(lv_event_t* event){auto& form=*static_cast<Form*>(lv_event_get_user_data(event));auto& service=CalendarSyncService::instance();
    const auto action=uintptr_t(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    const auto account=service.accountInfo(String("account-")+form.selected);
    const bool ok=account.phonebookEnabled&&(action==0?service.authorizeCapability(account.id,AccountCapability::Contacts):service.sync(form.selected));
    lv_label_set_text(form.status,ok?"Kontaktanfrage im Hintergrund gestartet.":"Konto speichern, Telefonbuch einschalten und Einstellungen entsperren; Dienst möglicherweise beschäftigt.");}
void slotClicked(lv_event_t* event) {
    auto& form = *static_cast<Form*>(lv_event_get_user_data(event));
    const auto slot = uint8_t(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event)))));
    if (slot < 5) select(form, slot);
}
void providerChanged(lv_event_t* event) {
    auto& form = *static_cast<Form*>(lv_event_get_user_data(event));
    lv_label_set_text(form.hint, EmailAccountService::providerHint(EmailProvider(lv_dropdown_get_selected(form.provider))));
    refreshOAuth(form);form.qrUrl="";if(form.loginQr)lv_obj_add_flag(form.loginQr,LV_OBJ_FLAG_HIDDEN);
}
void configureOAuth(lv_event_t* event){
    auto& form=*static_cast<Form*>(lv_event_get_user_data(event));
    const bool ok=CalendarSyncService::instance().configure(EmailProvider(lv_dropdown_get_selected(form.provider)),
        lv_textarea_get_text(form.clientId),lv_textarea_get_text(form.redirect),lv_textarea_get_text(form.clientSecret),purpose(form));
    eraseInput(form.clientSecret);
    lv_label_set_text(form.status,ok?"OAuth-Anwendung sicher gespeichert.":"Nicht gespeichert: Einstellungen entsperren und OAuth-Anwendungsdaten prüfen. Google benötigt HTTPS-Rückleitung und Client-Secret.");
}
void oauthAction(lv_event_t* event){
    auto& form=*static_cast<Form*>(lv_event_get_user_data(event));
    const uintptr_t action=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    const auto provider=EmailProvider(lv_dropdown_get_selected(form.provider));
    const bool ok=action==0?CalendarSyncService::instance().authorizeCapability(String("account-")+form.selected,purpose(form)==OAuthPurpose::Calendar?AccountCapability::Calendar:AccountCapability::Profile):CalendarSyncService::instance().sync(form.selected);
    lv_label_set_text(form.status,ok?"Anfrage läuft. Anmeldung beim Anbieter abschließen; der Status wird unten angezeigt.":"Anfrage nicht gestartet. Konto speichern, Client-ID/HTTPS-Rückleitung prüfen und Einstellungen entsperren.");
}
void updateOAuth(lv_timer_t* timer){
    auto& form=*static_cast<Form*>(lv_timer_get_user_data(timer));const auto info=CalendarSyncService::instance().info(form.selected);
    String text=info.message;
    text+="\nKontakte: "+info.contactsMessage;
    if(info.userCode.length())text+="\nGerätecode: "+info.userCode;
    if(form.qrGuide){const auto p=EmailProvider(lv_dropdown_get_selected(form.provider));const char* message=p==EmailProvider::Microsoft?"QR scannen, auf der Microsoft-Seite den Gerätecode eingeben und Anmeldung bestätigen.":p==EmailProvider::Google?"QR scannen und Google-Zugriff am Handy bestätigen. Kalender benötigt registriertes HTTPS-OAuth.":"iCloud: QR öffnet die Kontenseite. Apple App-Passwort dort eingeben.";if(strcmp(lv_label_get_text(form.qrGuide),message))lv_label_set_text(form.qrGuide,message);}
    text+="\nKontenverwaltung: https://"+SettingsService::instance().snapshot().hostname+".local/accounts";
    if(strcmp(lv_label_get_text(form.oauthStatus),text.c_str()))lv_label_set_text(form.oauthStatus,text.c_str());
    String url=info.authorizationUrl;
    if(!url.length()&&EmailProvider(lv_dropdown_get_selected(form.provider))==EmailProvider::Apple){const String ip=NetworkService::instance().ip();if(ip.length()&&ip!="0.0.0.0")url="https://"+ip+"/accounts?setup=icloud";}
    if(url!=form.qrUrl){form.qrUrl=url;lv_obj_add_flag(form.loginQr,LV_OBJ_FLAG_HIDDEN);
        if(url.startsWith("https://")&&url.length()<=1024&&QrGenerator::update(form.loginQr,url))lv_obj_remove_flag(form.loginQr,LV_OBJ_FLAG_HIDDEN);}
    const auto account=CalendarSyncService::instance().accountInfo(String("account-")+form.selected);
    if(account.phonebookEnabled)lv_obj_add_state(form.phonebook,LV_STATE_CHECKED);else lv_obj_remove_state(form.phonebook,LV_STATE_CHECKED);
}
void save(lv_event_t* event) {
    auto& form = *static_cast<Form*>(lv_event_get_user_data(event));
    if (!SecurityService::instance().settingsAllowed() || SecurityService::instance().tabletLocked()) {
        eraseInput(form.password); SecurityUi::requestSettingsUnlock();
        lv_label_set_text(form.status, "Einstellungen entsperren, danach Passwort erneut eingeben."); return;
    }
    const auto provider=EmailProvider(lv_dropdown_get_selected(form.provider));
    const bool ok = provider==EmailProvider::Other?CalendarSyncService::instance().configureCalDav(form.selected,AccountProvider::CalDav,lv_textarea_get_text(form.davServer),lv_textarea_get_text(form.address),lv_textarea_get_text(form.password)):
        provider==EmailProvider::Apple?CalendarSyncService::instance().configureCalDav(form.selected,AccountProvider::ICloud,"",lv_textarea_get_text(form.address),lv_textarea_get_text(form.password)):
        EmailAccountService::instance().save(form.selected,provider, lv_textarea_get_text(form.address), lv_textarea_get_text(form.password));
    eraseInput(form.password); form.deleteArmedAt = 0;lv_label_set_text(form.deleteLabel,"Konto entfernen");
    if (ok) select(form, form.selected);
    lv_label_set_text(form.status, ok ? "Verschlüsselt gespeichert · Verbindung noch nicht geprüft" :
        "Nicht gespeichert. Adresse, Passwort und freien Konfigurationsspeicher prüfen. Bei geänderter Identität Passwort erneut eingeben.");
}
void remove(lv_event_t* event) {
    auto& form = *static_cast<Form*>(lv_event_get_user_data(event));
    if (!form.deleteArmedAt || uint32_t(millis() - form.deleteArmedAt) > 15000) {
        form.deleteArmedAt = millis(); lv_label_set_text(form.deleteLabel, "Ja, Konto endgültig entfernen"); return;
    }
    form.deleteArmedAt = 0;
    if (!SecurityService::instance().settingsAllowed()) { eraseInput(form.password); SecurityUi::requestSettingsUnlock(); return; }
    if(form.data)lv_obj_clean(form.data);
    const bool ok = CalendarSyncService::instance().removeAccount(String("account-")+form.selected);
    if (ok) select(form, form.selected);
    lv_label_set_text(form.status, ok ? "Konto, Zugangsdaten und lokale Kontaktdateien werden im Hintergrund entfernt." : "Konto konnte nicht entfernt werden.");
    lv_label_set_text(form.deleteLabel, "Konto entfernen");
}
void destroyed(lv_event_t* event) {
    auto* form = static_cast<Form*>(lv_event_get_user_data(event));
    if (lv_event_get_target(event) != form->root) return;
    if(form->timer)lv_timer_delete(form->timer);
    eraseInput(form->clientSecret, true);eraseInput(form->password, true); delete form;
}
void testDav(lv_event_t* event){auto& form=*static_cast<Form*>(lv_event_get_user_data(event));const bool ok=CalendarSyncService::instance().testCalDav(form.selected);
 lv_label_set_text(form.status,ok?"Verbindungstest gestartet. Ergebnis erscheint im Kontostatus.":"Erst iCloud-/CalDAV-Konto und App-Passwort speichern; Einstellungen entsperren.");}
lv_obj_t* action(lv_obj_t* parent, const char* text, lv_event_cb_t callback, Form* form) {
    auto* button = lv_button_create(parent); SettingsWidgets::navButton(button, false);
    lv_obj_set_width(button, LV_PCT(100)); lv_obj_set_height(button, 50);
    SettingsWidgets::label(button, text, 0); lv_obj_center(lv_obj_get_child(button, 0));
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, form); return button;
}
}
void EmailAccountsUi::build(lv_obj_t* parent, lv_event_cb_t focus, void* context) {
    auto* form = new(std::nothrow) Form{};
    if (!form) { SettingsWidgets::section(parent, "Kontoverwaltung nicht verfügbar."); return; }
    form->focus=focus;form->focusContext=context;
    SettingsWidgets::section(parent, "Kalender verbinden: 1. Anbieter wählen · 2. Konto speichern · 3. Kalender freigeben und synchronisieren. Bei iCloud ein Apple App-Passwort verwenden. Bis zu fünf Konten.");
    auto* body = SettingsWidgets::row(parent, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    form->root = body;
    auto* list = SettingsWidgets::content(body, false); lv_obj_set_width(list, LV_PCT(36));lv_obj_set_flex_grow(list,0);lv_obj_set_height(list,LV_SIZE_CONTENT);
    auto* editor = SettingsWidgets::content(body, false); lv_obj_set_flex_grow(editor,1);lv_obj_set_height(editor,LV_SIZE_CONTENT);
    SettingsWidgets::card(editor);lv_obj_set_style_pad_all(editor,18,0);
    for (uint8_t slot = 0; slot < 5; ++slot) {
        auto* button = action(list, "", slotClicked, form); lv_obj_set_height(button, 86);
        auto* label = lv_obj_get_child(button, 0);lv_obj_set_width(label,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        lv_obj_set_user_data(button,reinterpret_cast<void*>(uintptr_t(slot)));form->slots[slot]=button;
    }
    SettingsWidgets::label(editor,"Konto einrichten",0,&lv_font_montserrat_24);
    SettingsWidgets::field(editor,"Anbieter");form->provider=lv_dropdown_create(editor);lv_obj_set_width(form->provider,LV_PCT(100));
    lv_dropdown_set_options(form->provider,"Google / Gmail\nMicrosoft / Outlook\nApple / iCloud (CalDAV)\nGMX\nWEB.DE\nYahoo\nTelekom / T-Online\nEigener CalDAV-Server");
    lv_obj_add_event_cb(form->provider,providerChanged,LV_EVENT_VALUE_CHANGED,form);
    auto field = [&](const char* title, bool secret) {
        SettingsWidgets::field(editor,title);auto* input=lv_textarea_create(editor);SettingsWidgets::darkInput(input);
        lv_obj_set_width(input,LV_PCT(100));lv_obj_set_height(input,52);lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,secret?192:254);
        if(secret){lv_textarea_set_password_mode(input,true);lv_textarea_set_password_show_time(input,0);}
        for(auto code:{LV_EVENT_PRESSED,LV_EVENT_SHORT_CLICKED})lv_obj_add_event_cb(input,focus,code,context);
        return input;
    };
    form->address=field("E-Mail-Adresse / CalDAV-Benutzername (OAuth übernimmt sie)",false);form->password=field("Passwort / App-Passwort",true);
    form->passwordLabel=lv_obj_get_child(editor,lv_obj_get_index(form->password)-1);
    form->davServer=field("CalDAV HTTPS-Server, optional Port / Basis-Pfad",false);lv_textarea_set_max_length(form->davServer,512);
    form->davServerLabel=lv_obj_get_child(editor,lv_obj_get_index(form->davServer)-1);
    form->hint=SettingsWidgets::label(editor,"",0,&lv_font_montserrat_14);lv_obj_set_width(form->hint,LV_PCT(100));lv_label_set_long_mode(form->hint,LV_LABEL_LONG_WRAP);
    action(editor,"Konto sicher speichern",save,form);
    form->davTest=action(editor,"CalDAV-Verbindung testen",testDav,form);
    auto* deleteButton=action(editor,"Konto entfernen",remove,form);form->deleteLabel=lv_obj_get_child(deleteButton,0);
    form->status=SettingsWidgets::label(editor,"",0,&lv_font_montserrat_14);lv_obj_set_width(form->status,LV_PCT(100));lv_label_set_long_mode(form->status,LV_LABEL_LONG_WRAP);
    SettingsWidgets::field(editor,"Telefonbuch für dieses Konto (standardmäßig aus)");form->phonebook=lv_switch_create(editor);
    lv_obj_add_event_cb(form->phonebook,phonebookChanged,LV_EVENT_VALUE_CHANGED,form);
    SettingsWidgets::section(editor,"Lokale Kontakte bleiben von Anbieter-Kontakten getrennt. Google / Microsoft: zusätzliche Lesefreigabe; iCloud / eigener Server: CardDAV mit dem gespeicherten App-Passwort. Keine Cloud-Schreibzugriffe.");
    auto* contactConnect=action(editor,"Kontakte verbinden / freigeben",contactAction,form);lv_obj_set_user_data(contactConnect,reinterpret_cast<void*>(uintptr_t(0)));
    auto* contactSync=action(editor,"Kontakte jetzt synchronisieren",contactAction,form);lv_obj_set_user_data(contactSync,reinterpret_cast<void*>(uintptr_t(1)));
    action(editor,"Telefonbuch & Termindetails anzeigen",openData,form);
    form->oauth=SettingsWidgets::content(editor,false);lv_obj_set_width(form->oauth,LV_PCT(100));lv_obj_set_height(form->oauth,LV_SIZE_CONTENT);
    SettingsWidgets::label(form->oauth,"OAuth & Kalender",0,&lv_font_montserrat_24);
    SettingsWidgets::section(form->oauth,"Schritt 1: Anbieter und Kontodaten speichern. Schritt 2: OAuth-Anwendung einrichten (eigene Client-ID nötig). Schritt 3: Kalender freigeben und synchronisieren. Microsoft: Gerätecode; Google Kalender: Web-OAuth mit registrierter HTTPS-Rückleitung. Keine Anbieter-Passwörter eingeben.");
    SettingsWidgets::section(form->oauth,"Google Kalender: Jeder Besitzer registriert seine eigene OAuth-Webanwendung bei Google Cloud, aktiviert die Calendar API und richtet einen HTTPS-Callback /oauth/callback ein. Client-ID und Rückleitung hier speichern; Client-Secret nur in dieses geschützte Displayfeld eingeben. Weitere Schritte per QR am Handy öffnen.");
    action(form->oauth,"Google-OAuth-Einrichtung am Handy öffnen",[](lv_event_t* event){auto& form=*static_cast<Form*>(lv_event_get_user_data(event));
        QrDialog::show("Eigene Google OAuth-App einrichten","https://console.cloud.google.com/auth/clients",false,form.root);
    },form);
    form->purpose=lv_dropdown_create(form->oauth);lv_dropdown_set_options(form->purpose,"Profil / Gerätecode\nKalender freigeben");lv_dropdown_set_selected(form->purpose,1);
    lv_obj_add_event_cb(form->purpose,providerChanged,LV_EVENT_VALUE_CHANGED,form);
    auto oauthField=[&](const char* title,bool secret){
        SettingsWidgets::field(form->oauth,title);auto* input=lv_textarea_create(form->oauth);SettingsWidgets::darkInput(input);
        lv_obj_set_width(input,LV_PCT(100));lv_obj_set_height(input,52);lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,secret?256:254);
        if(secret){lv_textarea_set_password_mode(input,true);lv_textarea_set_password_show_time(input,0);}
        for(auto code:{LV_EVENT_PRESSED,LV_EVENT_SHORT_CLICKED})lv_obj_add_event_cb(input,focus,code,context);
        return input;
    };
    form->clientId=oauthField("Client-ID",false);form->redirect=oauthField("Google HTTPS-Rückleitung (Microsoft: nicht benötigt)",false);
    form->redirectLabel=lv_obj_get_child(form->oauth,lv_obj_get_index(form->redirect)-1);
    form->clientSecret=oauthField("Google Client-Secret (Microsoft: nicht benötigt)",true);
    form->secretLabel=lv_obj_get_child(form->oauth,lv_obj_get_index(form->clientSecret)-1);
    action(form->oauth,"OAuth-Anwendung speichern",configureOAuth,form);
    auto* connectButton=action(form->oauth,"Im Browser verbinden",oauthAction,form);lv_obj_set_user_data(connectButton,reinterpret_cast<void*>(uintptr_t(0)));
    auto* syncButton=action(form->oauth,"Kalender jetzt synchronisieren",oauthAction,form);lv_obj_set_user_data(syncButton,reinterpret_cast<void*>(uintptr_t(1)));
    form->oauthStatus=SettingsWidgets::label(form->oauth,"",0,&lv_font_montserrat_14);lv_obj_set_width(form->oauthStatus,LV_PCT(100));lv_label_set_long_mode(form->oauthStatus,LV_LABEL_LONG_WRAP);
    SettingsWidgets::section(editor,"iCloud am Handy: QR öffnen, Apple App-Passwort auf account.apple.com erstellen, auf der lokalen HTTPS-Kontenseite eingeben und Verbindung testen. Das normale Apple-Passwort wird nicht benötigt.");
    form->qrGuide=SettingsWidgets::label(editor,"QR-Code erscheint nach Start der Anmeldung. Keine Zugangsdaten im QR-Code.",0,&lv_font_montserrat_14);lv_obj_set_width(form->qrGuide,LV_PCT(100));lv_label_set_long_mode(form->qrGuide,LV_LABEL_LONG_WRAP);
    form->loginQr=lv_qrcode_create(editor);lv_qrcode_set_size(form->loginQr,260);lv_qrcode_set_dark_color(form->loginQr,lv_color_black());lv_qrcode_set_light_color(form->loginQr,lv_color_white());lv_obj_set_style_border_width(form->loginQr,16,0);lv_obj_set_style_border_color(form->loginQr,lv_color_white(),0);lv_obj_add_flag(form->loginQr,LV_OBJ_FLAG_HIDDEN);
    action(editor,"QR-Code groß anzeigen",[](lv_event_t* e){auto& form=*static_cast<Form*>(lv_event_get_user_data(e));
        if(!form.qrUrl.startsWith("https://")){lv_label_set_text(form.status,"Zuerst Konto bzw. Anmeldung starten; anschließend QR öffnen.");return;}
        QrDialog::show("Konto am Smartphone verbinden",form.qrUrl,false,form.root);
    },form);
    form->data=SettingsWidgets::content(parent,false);lv_obj_set_flex_grow(form->data,0);lv_obj_set_height(form->data,LV_SIZE_CONTENT);
    form->timer=lv_timer_create(updateOAuth,1000,form);if(form->timer)updateOAuth(form->timer);
    SettingsWidgets::section(parent,"Passwörter und OAuth-Tokens werden nicht an Apps ausgegeben. Google / Microsoft: Standardkalender lesend, nächste 30 Tage, maximal 32 Termine je Konto.");
    lv_obj_add_event_cb(body,destroyed,LV_EVENT_DELETE,form);select(*form,0);
}
}
