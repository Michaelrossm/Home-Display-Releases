#include "PhonebookApp.h"
#include "../../services/SecurityService.h"
#include "PhonebookFilter.h"
#include "../calendar/CalendarService.h"
#include "../../services/NetworkWorker.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/UiTheme.h"
#include "../../ui/InputKeyboard.h"
#include "../../ui/CountryPicker.h"
#include "../../ui/SecurityUi.h"
#include "../../ui/QrDialog.h"
#include "../../ui/UiShell.h"
#include "../../services/PersonName.h"
#include "../../services/PostalLookupJob.h"
#include "../../core/PsramArray.h"
#include <algorithm>
namespace hp {
struct PhonebookPage {
    PhonebookApp* app=nullptr;
    lv_obj_t *root=nullptr,*body=nullptr,*status=nullptr,*keyboard=nullptr,*filter=nullptr,*search=nullptr,*assignment=nullptr,*tools=nullptr,*back=nullptr,*fields[10]{};
    struct ValueInputs {lv_obj_t *label=nullptr,*value=nullptr;};
    std::vector<ValueInputs> phoneInputs,emailInputs;
    struct AddressInputs {lv_obj_t* fields[8]{};};std::vector<AddressInputs> addressInputs;
    lv_timer_t* timer=nullptr;
    LocalPhonebook data;
    PhonebookContact contact;
    PhonebookUser user;
    enum class View {Contacts,Details,Management,Users,ContactEditor,UserEditor,Exchange}view=View::Contacts;
    String selectedUser;
    size_t offset=0;
    uint32_t postalJob=0;size_t postalAddress=0;String postalCode,postalCountry,postalBefore[3];
    JsonDocument postalResult;lv_obj_t* postalChoice=nullptr;
    void cancelPostal(){if(postalJob)LuaJobService::instance().cancel("@phonebook-postal",postalJob);postalJob=0;if(postalChoice)lv_obj_delete(postalChoice);postalChoice=nullptr;postalResult.clear();}
    void applyPostal(unsigned selected){
        if(view!=View::ContactEditor||postalAddress>=addressInputs.size())return;
        auto& row=addressInputs[postalAddress];
        if(postalCode!=lv_textarea_get_text(row.fields[3])||postalCountry!=lv_textarea_get_text(row.fields[6]))return;
        auto place=postalResult["places"][selected];if(place.isNull())return;
        const char* keys[]={"city","region","country"};
        for(unsigned i=0;i<3;++i){auto* field=row.fields[i+4];if(postalBefore[i]==lv_textarea_get_text(field)&&(!postalBefore[i].length()||i==2))lv_textarea_set_text(field,place[keys[i]]|"");}
        lv_label_set_text(status,"Ortsdaten ergänzt. Kontakt zum Übernehmen speichern.");
    }
    void startPostal(size_t index){
        if(!available()||contact.readOnly||index>=addressInputs.size())return;
        cancelPostal();postalAddress=index;auto& row=addressInputs[index];postalCode=lv_textarea_get_text(row.fields[3]);postalCountry=lv_textarea_get_text(row.fields[6]);
        for(unsigned i=0;i<3;++i)postalBefore[i]=lv_textarea_get_text(row.fields[i+4]);
        LuaJobService::Request request;request.owner="@phonebook-postal";request.kind=5;request.handler=executePostalLookupJob;request.lane=LuaJobService::Request::Lane::Transfer;
        request.required=request.permissions=perm(AppPermission::NetworkInternet);request.completion="http.completed";
        JsonDocument args;args["postal_code"]=postalCode;args["country"]=postalCountry;serializeJson(args,request.body);
        postalJob=LuaJobService::instance().submit(request);lv_label_set_text(status,postalJob?"Ortsdaten werden gesucht …":"Netzwerk belegt. Bitte erneut versuchen.");
    }
    uint32_t searchChangedAt=0;bool searchPending=false;
    uint32_t seenRevision=0;
    uint32_t nextRevisionCheckAt=0; // Only compare the phonebook revision every 2 seconds.
    bool loaded=false,confirmDelete=false,reloadWhenIdle=false,exportMode=false;
    static PhonebookPage& self(lv_event_t* e){return *static_cast<PhonebookPage*>(lv_event_get_user_data(e));}
    lv_obj_t* label(lv_obj_t* parent,const char* value){auto* l=lv_label_create(parent);lv_label_set_text(l,value);lv_obj_set_width(l,LV_PCT(100));return l;}
    lv_obj_t* button(lv_obj_t* parent,const char* title,lv_event_cb_t fn){auto* b=lv_button_create(parent);lv_obj_set_size(b,160,48);UiTheme::instance().stylePrimaryButton(b);
        auto* l=lv_label_create(b);lv_label_set_text(l,title);lv_obj_set_width(l,LV_PCT(100));lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);lv_obj_add_event_cb(b,fn,LV_EVENT_CLICKED,this);return b;}
    void hideKeyboard(){lv_keyboard_set_textarea(keyboard,nullptr);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_pad_bottom(root,16,0);}
    static void focus(lv_event_t* e){auto& p=self(e);InputKeyboard::bind(p.keyboard,static_cast<lv_obj_t*>(lv_event_get_target(e)));
        lv_obj_set_style_pad_bottom(p.root,276,0);lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p.keyboard);lv_obj_scroll_to_view(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_ANIM_OFF);}
    static void key(lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL)self(e).hideKeyboard();}
    static void outside(lv_event_t* e){auto& p=self(e);if(lv_obj_has_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN))return;
        for(auto* o=static_cast<lv_obj_t*>(lv_event_get_target(e));o;o=lv_obj_get_parent(o))if(o==p.keyboard||lv_obj_check_type(o,&lv_textarea_class))return;p.hideKeyboard();}
    void bubble(lv_obj_t* o){if(o==keyboard)return;lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)bubble(lv_obj_get_child(o,i));}
    lv_obj_t* input(lv_obj_t* parent,const char* name,const String& value,size_t limit,bool multiline=false){label(parent,name);auto* t=lv_textarea_create(parent);
        lv_obj_set_width(t,LV_PCT(100));lv_textarea_set_one_line(t,!multiline);lv_textarea_set_max_length(t,limit);lv_textarea_set_text(t,value.c_str());
        if(multiline)lv_obj_set_height(t,120);SettingsWidgets::darkInput(t);lv_obj_add_event_cb(t,focus,LV_EVENT_FOCUSED,this);lv_obj_add_event_cb(t,focus,LV_EVENT_CLICKED,this);return t;}
    void clear(bool dismissKeyboard=true){cancelPostal();if(dismissKeyboard)hideKeyboard();lv_obj_clean(body);confirmDelete=false;}
    void setView(View next){view=next;searchPending=false;
        if(next==View::Contacts){lv_obj_remove_flag(tools,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);}
        else{lv_obj_add_flag(tools,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(back,LV_OBJ_FLAG_HIDDEN);}}
    bool available(){if(loaded&&!app->busy_.load())return true;lv_label_set_text(status,"Zuerst Laden abschließen; bei Fehler Aktualisieren drücken.");return false;}
    void options(){String names="Alle Kontakte";unsigned selected=0;for(size_t i=0;i<data.users.size();++i){names+="\n"+data.users[i].name;if(data.users[i].id==selectedUser)selected=i+1;}
        if(names!=lv_dropdown_get_options(filter))lv_dropdown_set_options(filter,names.c_str());lv_dropdown_set_selected(filter,selected);if(!selected)selectedUser="";}
    void contacts(bool liveSearch=false){clear(!liveSearch);setView(View::Contacts);
        const String owner=selectedUser;
        std::vector<size_t> matches;const char* query=lv_textarea_get_text(search);
        for(size_t i=0;i<data.contacts.size();++i)if(phonebookMatches(data.contacts[i],owner.c_str(),query))matches.push_back(i);
        std::stable_sort(matches.begin(),matches.end(),[this](size_t a,size_t b){return phonebookNameLess(data.contacts[a].name.c_str(),data.contacts[b].name.c_str());});
        if(offset>=matches.size())offset=0;
        lv_label_set_text(status,(String(matches.size())+" Kontakte").c_str());
        for(size_t n=offset;n<matches.size()&&n<offset+20;++n){const auto index=matches[n];const auto& c=data.contacts[index];
            String title=c.name+"\n"+(c.company.length()?c.company+" · ":String())+(c.mobile.length()?c.mobile:c.phone.length()?c.phone:c.email)+(c.readOnly?" · Cloud":String());
            auto* b=button(body,title.c_str(),[](lv_event_t* e){auto& p=self(e);if(!p.available())return;
                const size_t i=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
                if(i<p.data.contacts.size()){p.contact=p.data.contacts[i];p.details();}});
            lv_obj_set_size(b,LV_PCT(100),76);UiTheme::instance().styleCard(b);lv_obj_set_style_radius(b,0,0);lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_side(b,LV_BORDER_SIDE_BOTTOM,0);lv_obj_set_style_pad_left(b,20,0);
            auto* text=lv_obj_get_child(b,0);lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_style_text_font(text,fontWithGermanFallback(&lv_font_montserrat_18),0);lv_obj_set_user_data(b,reinterpret_cast<void*>(index));}
        if(matches.empty())label(body,"Keine passenden Kontakte. Über Neuer Kontakt einen Eintrag anlegen.");
        if(matches.size()>20){auto* pages=SettingsWidgets::row(body,54);
        auto* prev=button(pages,"Zurück",[](lv_event_t* e){auto& p=self(e);p.offset=p.offset>=20?p.offset-20:0;p.contacts();});
        auto* next=button(pages,"Weiter",[](lv_event_t* e){auto& p=self(e);p.offset+=20;p.contacts();});
        if(!offset)lv_obj_add_state(prev,LV_STATE_DISABLED);if(offset+20>=matches.size())lv_obj_add_state(next,LV_STATE_DISABLED);}bubble(root);
    }
    void detailField(const char* title,const String& value){if(!value.length())return;
        auto* card=SettingsWidgets::content(body,false);lv_obj_set_size(card,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_grow(card,0);
        auto* caption=label(card,title);lv_obj_set_style_text_color(caption,UiTheme::instance().textSecondary(),0);label(card,value.c_str());}
    void details(){clear();setView(View::Details);
        button(body,"Kontakt als QR",[](lv_event_t* e){auto& p=self(e);String card;if(PhonebookService::instance().exportVcard(p.contact,card))QrDialog::show(p.contact.name.c_str(),card,false,p.root);else lv_label_set_text(p.status,"Kontakt konnte nicht als vCard erzeugt werden.");});
        auto* name=label(body,contact.name.c_str());lv_obj_set_style_text_font(name,fontWithGermanFallback(&lv_font_montserrat_24),0);
        detailField("Firma",contact.company);detailField("Position",contact.jobTitle);
        for(const auto& value:contact.phones)detailField(value.label.c_str(),value.value.c_str());
        if(contact.phones.empty()){detailField("Mobil",contact.mobile);detailField("Telefon",contact.phone);}
        for(const auto& value:contact.emails)detailField(value.label.c_str(),value.value.c_str());
        if(contact.emails.empty())detailField("E-Mail",contact.email);
        for(const auto& a:contact.addresses){String text=String(a.street.c_str())+" "+a.houseNumber.c_str()+"\n"+a.postalCode.c_str()+" "+a.city.c_str();if(!a.region.empty())text+="\n"+String(a.region.c_str());if(!a.country.empty())text+="\n"+String(a.country.c_str());detailField(a.label.c_str(),text);}
        if(contact.addresses.empty()&&(contact.street.length()||contact.houseNumber.length()||contact.postalCode.length()||contact.city.length()||contact.country.length()))detailField("Adresse",contact.street+" "+contact.houseNumber+"\n"+contact.postalCode+" "+contact.city+"\n"+contact.country);
        detailField("Geburtstag",ContactProtocol::birthdayText(contact.birthday).c_str());detailField("Notizen",contact.notes);
        if(contact.readOnly)label(body,"Cloud-Kontakt · Änderungen beim Anbieter vornehmen");
        else button(body,"Bearbeiten",[](lv_event_t* e){auto& p=self(e);if(p.available())p.editor();});
        auto* exportButton=button(body,"vCard exportieren",[](lv_event_t* e){auto& p=self(e);if(p.available())p.exchange(true);});lv_obj_set_width(exportButton,220);bubble(root);
    }
    void management(){clear();setView(View::Management);label(body,"Telefonbuch verwalten");
        button(body,"Benutzer",[](lv_event_t*){UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}");});
        auto* import=button(body,"vCard importieren",[](lv_event_t* e){auto& p=self(e);if(!p.available())return;p.contact={};p.contact.userId=p.selectedUser;p.exchange(false);});lv_obj_set_width(import,220);
        button(body,"Aktualisieren",[](lv_event_t* e){auto& p=self(e);if(!p.app->queue(PhonebookApp::Operation::Load))lv_label_set_text(p.status,"Laufenden Auftrag abwarten.");});
        button(body,"PIN freigeben",[](lv_event_t*){SecurityUi::requestSettingsUnlock();});bubble(root);
    }
    void users(){UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}");}
    void exchange(bool exporting){clear();setView(View::Exchange);exportMode=exporting;
        label(body,exporting?"Kontakt als vCard 4.0 exportieren":"Eine vCard 3.0 / 4.0 importieren");
        label(body,"Ordner in Dateien: data:/contacts/ · eine Person pro .vcf, maximal 16 KiB. Export überschreibt keine vorhandene Datei.");
        fields[0]=input(body,"Dateiname (ohne Ordner)",exporting?contact.id+".vcf":String("kontakt.vcf"),80);
        button(body,exporting?"Exportieren":"Importieren",[](lv_event_t* e){auto& p=self(e);if(!p.available())return;
            if(p.app->queue(p.exportMode?PhonebookApp::Operation::ExportVcard:PhonebookApp::Operation::ImportVcard,&p.contact,nullptr,lv_textarea_get_text(p.fields[0]))){p.hideKeyboard();lv_label_set_text(p.status,"vCard-Datei wird verarbeitet …");}});bubble(root);
    }
    bool draft(bool prune=false){
        if(contact.readOnly){lv_label_set_text(status,"Cloud-Kontakt ist schreibgeschützt. Änderungen beim Anbieter vornehmen.");return false;}
        contact.firstName=lv_textarea_get_text(fields[0]);contact.lastName=lv_textarea_get_text(fields[5]);contact.name=personDisplayName(contact.firstName,contact.lastName);contact.company=lv_textarea_get_text(fields[1]);contact.jobTitle=lv_textarea_get_text(fields[2]);contact.notes=lv_textarea_get_text(fields[3]);
        if(!ContactProtocol::parseBirthday(lv_textarea_get_text(fields[4]),contact.birthday)){lv_label_set_text(status,"Geburtstag: JJJJ-MM-TT oder --MM-TT (ohne Jahr). Datum prüfen.");return false;}
        const unsigned owner=lv_dropdown_get_selected(assignment);contact.userId=owner&&owner<=data.users.size()?data.users[owner-1].id:String();
        auto collect=[&](const std::vector<ValueInputs>& inputs,std::vector<ContactProtocol::Value>& out){out.clear();for(const auto& i:inputs){std::string value=lv_textarea_get_text(i.value);if(!prune||!value.empty())out.push_back({lv_textarea_get_text(i.label),value});}};
        collect(phoneInputs,contact.phones);collect(emailInputs,contact.emails);contact.addresses.clear();
        for(const auto& row:addressInputs){ContactProtocol::Address a;std::string* values[]={&a.label,&a.street,&a.houseNumber,&a.postalCode,&a.city,&a.region,&a.country};
            for(unsigned i=0;i<7;++i)*values[i]=lv_textarea_get_text(row.fields[i]);
            if(!prune||!a.street.empty()||!a.houseNumber.empty()||!a.postalCode.empty()||!a.city.empty()||!a.region.empty()||!a.country.empty())contact.addresses.push_back(std::move(a));}
        // Keep the legacy API's preview fields synchronized, not a second contact database.
        contact.email=contact.emails.empty()?"":contact.emails.front().value.c_str();contact.phone="";contact.mobile="";
        for(const auto& v:contact.phones){if(v.label=="Mobil"&&!contact.mobile.length())contact.mobile=v.value.c_str();else if(!contact.phone.length())contact.phone=v.value.c_str();}
        contact.street="";contact.houseNumber="";contact.postalCode="";contact.city="";contact.country="";
        if(!contact.addresses.empty()){const auto& a=contact.addresses.front();contact.street=a.street.c_str();contact.houseNumber=a.houseNumber.c_str();contact.postalCode=a.postalCode.c_str();contact.city=a.city.c_str();contact.country=a.country.c_str();}
        return true;
    }
    lv_obj_t* labelledInput(lv_obj_t* parent,const std::string& value){
        auto* t=input(parent,"Bezeichnung / eigene Bezeichnung",value.c_str(),48);
        auto* presets=lv_dropdown_create(parent);lv_dropdown_set_options(presets,"Bezeichnung wählen\nPrivat\nArbeit\nMobil\nZentrale\nFax\nSonstige");lv_obj_set_width(presets,LV_PCT(100));
        lv_obj_add_event_cb(presets,[](lv_event_t* e){auto* d=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!lv_dropdown_get_selected(d))return;char text[48];lv_dropdown_get_selected_str(d,text,sizeof(text));lv_textarea_set_text(static_cast<lv_obj_t*>(lv_event_get_user_data(e)),text);},LV_EVENT_VALUE_CHANGED,t);return t;
    }
    void valueSection(bool emails){
        auto& entries=emails?contact.emails:contact.phones;auto& inputs=emails?emailInputs:phoneInputs;
        label(body,emails?"E-Mail-Adressen · bis zu 8":"Telefonnummern · bis zu 8");
        for(size_t i=0;i<entries.size();++i){auto* card=SettingsWidgets::content(body,false);lv_obj_set_size(card,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_grow(card,0);
            inputs.push_back({labelledInput(card,entries[i].label),input(card,emails?"E-Mail-Adresse":"Telefonnummer",entries[i].value.c_str(),emails?254:96)});
            if(!emails)InputKeyboard::setMode(inputs.back().value,keyboardInput::Mode::Phone);
            auto* remove=button(card,"Entfernen",[](lv_event_t* e){auto& p=self(e);const uintptr_t key=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));if(!p.draft())return;
                auto& values=(key&0x100)?p.contact.emails:p.contact.phones;const size_t index=key&0xff;if(index<values.size())values.erase(values.begin()+index);p.editor();});
            lv_obj_set_user_data(remove,reinterpret_cast<void*>(i|(emails?0x100:0)));}
        auto* add=button(body,emails?"E-Mail hinzufügen":"Nummer hinzufügen",[](lv_event_t* e){auto& p=self(e);const bool email=lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))!=nullptr;
            if(!p.draft())return;auto& values=email?p.contact.emails:p.contact.phones;if(values.size()<8)values.push_back({"Privat",""});p.editor();});lv_obj_set_width(add,220);
        lv_obj_set_user_data(add,emails?reinterpret_cast<void*>(1):nullptr);if(entries.size()>=8)lv_obj_add_state(add,LV_STATE_DISABLED);
    }
    void editor(){clear();for(auto& field:fields)field=nullptr;phoneInputs.clear();emailInputs.clear();addressInputs.clear();legacyPersonName(contact.name,contact.firstName,contact.lastName);setView(View::ContactEditor);label(body,contact.id.length()?"Kontakt bearbeiten":"Neuer Kontakt");
        label(body,"Benutzer / Filterzuordnung");assignment=lv_dropdown_create(body);String names="Nicht zugeordnet";unsigned selected=0;
        for(size_t i=0;i<data.users.size();++i){names+="\n"+data.users[i].name;if(data.users[i].id==contact.userId)selected=i+1;}
        lv_dropdown_set_options(assignment,names.c_str());lv_dropdown_set_selected(assignment,selected);lv_obj_set_width(assignment,LV_PCT(100));
        const char* namesFields[]={"Vorname","Firma / Organisation","Position / Berufsbezeichnung","Notizen","Geburtstag · JJJJ-MM-TT oder --MM-TT","Nachname"};
        const String values[]={contact.firstName,contact.company,contact.jobTitle,contact.notes,ContactProtocol::birthdayText(contact.birthday).c_str(),contact.lastName};
        const unsigned limits[]={256,192,128,1024,10,256};
        auto* form=SettingsWidgets::content(body,false);lv_obj_set_size(form,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_grow(form,0);lv_obj_set_flex_flow(form,LV_FLEX_FLOW_ROW_WRAP);
        for(unsigned n=0;n<6;++n){const unsigned i=n==1?5:n>1?n-1:0;auto* cell=SettingsWidgets::content(form,false);lv_obj_set_size(cell,LV_PCT(i==3?100:48),LV_SIZE_CONTENT);lv_obj_set_flex_grow(cell,0);fields[i]=input(cell,namesFields[i],values[i],limits[i],i==3);}
        InputKeyboard::setMode(fields[4],keyboardInput::Mode::DateTime);
        label(body,"Geburtstage erscheinen automatisch im Kalender „Geburtstage“.");valueSection(false);valueSection(true);
        label(body,"Adressen · bis zu 4, getrennt für Privat und Arbeit");
        for(size_t n=0;n<contact.addresses.size();++n){const auto& a=contact.addresses[n];auto* card=SettingsWidgets::content(body,false);lv_obj_set_size(card,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_grow(card,0);
            AddressInputs row;row.fields[0]=labelledInput(card,a.label);
            const char* titles[]={"Straße","Hausnummer","Postleitzahl","Ort","Region / Bundesland","Land"};const std::string* values[]={&a.street,&a.houseNumber,&a.postalCode,&a.city,&a.region,&a.country};const unsigned limits[]={192,32,32,128,96,96};
            auto* form=SettingsWidgets::content(card,false);lv_obj_set_size(form,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_grow(form,0);lv_obj_set_flex_flow(form,LV_FLEX_FLOW_ROW_WRAP);
            for(unsigned i=0;i<6;++i){auto* cell=SettingsWidgets::content(form,false);lv_obj_set_size(cell,LV_PCT(48),LV_SIZE_CONTENT);lv_obj_set_flex_grow(cell,0);row.fields[i+1]=input(cell,titles[i],values[i]->c_str(),limits[i]);if(i==5)countryPicker(cell,row.fields[i+1]);}addressInputs.push_back(row);
            label(card,"PLZ-Suche verwendet das Standardland der Firmware oder Ihre Auswahl.");
            auto* lookup=button(card,"Ort zur PLZ suchen",[](lv_event_t* e){auto& p=self(e);p.startPostal(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))));});lv_obj_set_width(lookup,240);lv_obj_set_user_data(lookup,reinterpret_cast<void*>(n));
            auto* remove=button(card,"Adresse entfernen",[](lv_event_t* e){auto& p=self(e);const size_t n=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));if(!p.draft())return;if(n<p.contact.addresses.size())p.contact.addresses.erase(p.contact.addresses.begin()+n);p.editor();});lv_obj_set_width(remove,220);lv_obj_set_user_data(remove,reinterpret_cast<void*>(n));}
        auto* addAddress=button(body,"Adresse hinzufügen",[](lv_event_t* e){auto& p=self(e);if(!p.draft())return;if(p.contact.addresses.size()<4){ContactProtocol::Address a;a.label="Privat";p.contact.addresses.push_back(a);}p.editor();});lv_obj_set_width(addAddress,220);if(contact.addresses.size()>=4)lv_obj_add_state(addAddress,LV_STATE_DISABLED);
        auto* row=SettingsWidgets::row(body,54);button(row,"Speichern",[](lv_event_t* e){auto& p=self(e);if(!p.available())return;
            if(!p.draft(true))return;
            if(p.app->queue(PhonebookApp::Operation::SaveContact,&p.contact)){p.hideKeyboard();lv_label_set_text(p.status,"Kontakt wird gespeichert …");}});
        auto* remove=button(row,"Löschen",[](lv_event_t* e){auto& p=self(e);if(!p.available())return;
            if(!p.confirmDelete){p.confirmDelete=true;lv_label_set_text(p.status,"Kontakt löschen? Erneut Löschen zum Bestätigen drücken.");return;}
            if(p.app->queue(PhonebookApp::Operation::RemoveContact,&p.contact)){p.hideKeyboard();lv_label_set_text(p.status,"Kontakt wird gelöscht …");}});
        if(!contact.id.length()||contact.readOnly)lv_obj_add_state(remove,LV_STATE_DISABLED);
        if(contact.readOnly){label(body,"Cloud-Kontakt · Änderungen beim Anbieter vornehmen");for(auto* control:fields)if(control)lv_obj_add_state(control,LV_STATE_DISABLED);lv_obj_add_state(assignment,LV_STATE_DISABLED);lv_obj_add_state(lv_obj_get_child(row,0),LV_STATE_DISABLED);}
        bubble(root);
        auto* exportButton=button(body,"vCard exportieren",[](lv_event_t* e){auto& p=self(e);if(p.available()&&(p.contact.readOnly||p.draft(true)))p.exchange(true);});lv_obj_set_width(exportButton,220);if(!contact.id.length())lv_obj_add_state(exportButton,LV_STATE_DISABLED);bubble(root);
    }
    void pollPostal(){auto& p=*this;
        if(p.postalJob){LuaJobService::Result result;if(LuaJobService::instance().result("@phonebook-postal",p.postalJob,result)&&result.done){
            LuaJobService::instance().release("@phonebook-postal",p.postalJob);p.postalJob=0;
            if(!result.ok)lv_label_set_text(p.status,result.error.c_str());
            else if(!deserializeJson(p.postalResult,result.body)&&p.view==View::ContactEditor){
                auto places=p.postalResult["places"].as<JsonArray>();
                if(places.size()==1)p.applyPostal(0);
                else if(places.size()>1){p.postalChoice=lv_dropdown_create(p.body);String options="Ort auswählen";for(auto place:places)options+="\n"+place["city"].as<String>();lv_dropdown_set_options(p.postalChoice,options.c_str());lv_obj_set_width(p.postalChoice,LV_PCT(100));lv_obj_add_event_cb(p.postalChoice,[](lv_event_t* e){auto& p=self(e);const unsigned choice=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));if(choice)p.applyPostal(choice-1);},LV_EVENT_VALUE_CHANGED,&p);lv_obj_scroll_to_view(p.postalChoice,LV_ANIM_OFF);lv_label_set_text(p.status,"Mehrere Orte: passenden Ort auswählen.");}
            }
        }}
    }
    static void tick(lv_timer_t* timer){auto& p=*static_cast<PhonebookPage*>(lv_timer_get_user_data(timer));auto& app=*p.app;
        p.pollPostal();
        if(p.searchPending&&p.view==View::Contacts&&millis()-p.searchChangedAt>=250){p.searchPending=false;p.offset=0;p.contacts(true);}
        if(app.done_.exchange(false,std::memory_order_acquire)){
        if(app.work_.generation==app.generation_.load()){
            if(app.work_.ok){p.data=std::move(app.work_.data);p.loaded=true;p.seenRevision=app.work_.revision;p.options();p.offset=0;
                if(app.work_.operation==PhonebookApp::Operation::SaveUser)p.users();else p.contacts();
                lv_label_set_text(p.status,"Telefonbuch aktuell · lokal und offline verfügbar");}
            else lv_label_set_text(p.status,app.work_.conflict?"Kontakte wurden inzwischen geändert. Entwurf bleibt offen; vor dem Speichern neu laden.":"Auftrag fehlgeschlagen. PIN, Feldlängen, Speicher und Grenzen (128 Kontakte / 16 Benutzer) prüfen. Daten bleiben erhalten.");
        }app.busy_=false;
        }
        if(p.reloadWhenIdle&&!app.busy_.load()){
            p.reloadWhenIdle=false;
            if(!app.queue(PhonebookApp::Operation::Load))lv_label_set_text(p.status,"Laden noch nicht möglich. Bitte Aktualisieren drücken.");
        }
        // Keep the 200-ms UI tick for search debounce, postal results and job completion.
        // Only the background revision check runs every 2 seconds.
        if(p.loaded&&!app.busy_.load()&&p.view==View::Contacts&&
           int32_t(millis()-p.nextRevisionCheckAt)>=0){
            p.nextRevisionCheckAt=millis()+2000;
            if(p.seenRevision!=PhonebookService::instance().revision())
                app.queue(PhonebookApp::Operation::Load);
        }
    }
    static void deleted(lv_event_t* e){auto* p=static_cast<PhonebookPage*>(lv_event_get_user_data(e));if(lv_event_get_target(e)!=p->root)return;if(p->timer)lv_timer_delete(p->timer);
        p->cancelPostal();p->app->page_=nullptr;++p->app->generation_;p->~PhonebookPage();heap_caps_free(p);}
};
bool PhonebookApp::queue(Operation operation,const PhonebookContact* contact,const PhonebookUser* user,const String& filename){
    if(done_.load()&&!page_){done_=false;busy_=false;}
    bool expected=false;if(!busy_.compare_exchange_strong(expected,true))return false;
    work_.operation=operation;work_.generation=generation_.load();work_.ok=false;work_.contact=contact?*contact:PhonebookContact{};work_.user=user?*user:PhonebookUser{};
    work_.filename=filename;
    work_.expectedRevision=page_?page_->seenRevision:0;work_.conflict=false;
    if(NetworkWorker::instance().submitPriority(worker,this,"phonebook"))return true;busy_=false;return false;
}
void PhonebookApp::worker(void* context){auto& app=*static_cast<PhonebookApp*>(context);auto& w=app.work_;w.data={};
    if(w.generation==app.generation_.load()){
        std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
        if((w.operation==Operation::SaveContact||w.operation==Operation::RemoveContact)&&w.expectedRevision&&w.expectedRevision!=PhonebookService::instance().revision())w.conflict=true;
        else if(w.operation==Operation::Load)w.ok=PhonebookService::instance().loadDirectory(w.data);
        else if(w.operation==Operation::ImportVcard)w.ok=PhonebookService::instance().importVcardFile(w.filename,w.contact.userId,w.data);
        else if(w.operation==Operation::ExportVcard)w.ok=PhonebookService::instance().exportVcardFile(w.filename,w.contact,w.data);
        else{const auto action=w.operation==Operation::SaveUser?PhonebookService::LocalAction::SaveUser:w.operation==Operation::RemoveContact?PhonebookService::LocalAction::RemoveContact:PhonebookService::LocalAction::SaveContact;
            w.ok=PhonebookService::instance().changeLocal(action,w.contact,w.user,w.data);}
        w.revision=PhonebookService::instance().revision();
    }app.done_.store(true,std::memory_order_release);
}
void PhonebookApp::open(lv_obj_t* parent){close();++generation_;page_=allocatePsramArray<PhonebookPage>(1);if(!page_)return;
    auto& p=*page_;p.app=this;p.root=SettingsWidgets::content(parent,false);lv_obj_set_size(p.root,LV_PCT(100),LV_PCT(100));UiTheme::instance().styleScreen(p.root);
    lv_obj_set_style_bg_opa(p.root,LV_OPA_COVER,0);lv_obj_set_style_pad_all(p.root,16,0);lv_obj_set_style_text_font(p.root,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_obj_add_event_cb(p.root,PhonebookPage::deleted,LV_EVENT_DELETE,&p);
    auto* top=SettingsWidgets::row(p.root,58);auto* heading=p.label(top,"Telefonbuch");lv_obj_set_width(heading,0);lv_obj_set_flex_grow(heading,1);
    p.back=p.button(top,"Zur Kontaktliste",[](lv_event_t* e){PhonebookPage::self(e).contacts();});lv_obj_add_flag(p.back,LV_OBJ_FLAG_HIDDEN);
    p.button(top,"Neuer Kontakt",[](lv_event_t* e){auto& p=PhonebookPage::self(e);if(p.available()){p.contact={};p.contact.userId=p.selectedUser;p.editor();}});
    p.button(top,"Mehr",[](lv_event_t* e){PhonebookPage::self(e).management();});
    p.button(top,"Benutzer",[](lv_event_t*){UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}");});
    auto* tools=p.tools=SettingsWidgets::row(p.root,88);p.filter=lv_dropdown_create(tools);lv_dropdown_set_options(p.filter,"Alle Kontakte");lv_obj_set_width(p.filter,280);
    auto* searchCell=SettingsWidgets::content(tools,false);lv_obj_set_width(searchCell,0);lv_obj_set_flex_grow(searchCell,1);lv_obj_set_height(searchCell,LV_PCT(100));lv_obj_set_style_pad_row(searchCell,4,0);
    p.search=p.input(searchCell,"Kontakte durchsuchen",String(),64);lv_obj_set_height(p.search,46);lv_textarea_set_placeholder_text(p.search,"Name, Nummer, E-Mail, Firma oder Ort");
    lv_obj_add_event_cb(p.search,[](lv_event_t* e){auto& p=PhonebookPage::self(e);p.searchPending=true;p.searchChangedAt=millis();},LV_EVENT_VALUE_CHANGED,&p);
    lv_obj_add_event_cb(p.filter,[](lv_event_t* e){auto& p=PhonebookPage::self(e);const unsigned selected=lv_dropdown_get_selected(p.filter);p.selectedUser=selected&&selected<=p.data.users.size()?p.data.users[selected-1].id:String();p.offset=0;p.contacts();},LV_EVENT_VALUE_CHANGED,&p);
    p.body=SettingsWidgets::content(p.root,true);lv_obj_set_size(p.body,LV_PCT(100),0);lv_obj_set_style_pad_row(p.body,0,0);p.status=p.label(p.root,"Telefonbuch wird geladen …");lv_obj_set_height(p.status,28);
    p.keyboard=lv_keyboard_create(p.root);lv_obj_set_size(p.keyboard,LV_PCT(100),260);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_FLOATING);lv_obj_align(p.keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p.keyboard,PhonebookPage::key,LV_EVENT_ALL,&p);lv_obj_add_event_cb(p.root,PhonebookPage::outside,LV_EVENT_PRESSED,&p);p.bubble(p.root);
    p.timer=lv_timer_create(PhonebookPage::tick,200,&p);
    if(!queue(Operation::Load)){p.reloadWhenIdle=busy_.load();lv_label_set_text(p.status,p.reloadWhenIdle?"Vorheriger Auftrag wird beendet, danach wird neu geladen …":"Laden noch nicht möglich. Bitte Aktualisieren drücken.");}
}
void PhonebookApp::close(){++generation_;if(page_&&page_->root)lv_obj_delete(page_->root);}
}
