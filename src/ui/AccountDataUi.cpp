#include "AccountDataUi.h"
#include "InputKeyboard.h"
#include "SettingsWidgets.h"
#include "../services/CloudAccountService.h"
#include "../services/PhonebookService.h"
#include "../services/NetworkWorker.h"
#include "../services/ContactProtocol.h"
#include <memory>
#include <atomic>
#include <new>
#include <time.h>
#include <esp_random.h>
namespace hp {namespace {
struct Snapshot {
 String id,email,message;std::vector<PhonebookContact> contacts;std::vector<EventRecord> events;
 std::vector<std::unique_ptr<EventDetails>> details;
 SemaphoreHandle_t mutex=xSemaphoreCreateMutex();std::atomic<bool> alive{true},busy{false},ready{false};
 bool bookReady=false;~Snapshot(){if(mutex)vSemaphoreDelete(mutex);}
};
struct Lock{SemaphoreHandle_t m;explicit Lock(SemaphoreHandle_t value):m(value){if(m)xSemaphoreTake(m,portMAX_DELAY);}~Lock(){if(m)xSemaphoreGive(m);}};
struct Page {
 lv_obj_t *root,*status,*list,*detail,*search;lv_obj_t* fields[11]{};lv_timer_t* timer=nullptr;
 std::shared_ptr<Snapshot> state;std::vector<PhonebookContact> contacts;std::vector<EventRecord> events;
 std::vector<std::unique_ptr<EventDetails>> details;String editingId;bool bookReady=false,contactsTab=true;
 lv_event_cb_t focus=nullptr;void* context=nullptr;
};
struct Job{std::shared_ptr<Snapshot> state;bool save=false;std::vector<PhonebookContact> local;};
void load(void* pointer){std::unique_ptr<Job> job(static_cast<Job*>(pointer));auto s=job->state;
 if(!s->alive){s->busy=false;return;}auto& accounts=CloudAccountService::instance();const auto account=accounts.accountInfo(s->id);
 if(account.email!=s->email||!account.enabled){Lock lock(s->mutex);s->contacts.clear();s->events.clear();s->details.clear();s->bookReady=false;s->message="Konto geändert oder deaktiviert. Ansicht erneut öffnen.";s->ready=true;s->busy=false;return;}
 String message;auto& book=PhonebookService::instance();
 if(job->save){if(!book.save(s->id,job->local))message="Nicht gespeichert. Einstellungen entsperren, Konto und freien Speicher prüfen.";else message="Lokaler Kontakt gespeichert. Anbieter-Kontakte bleiben unverändert.";}
 std::vector<PhonebookContact> contacts;const bool bookReady=account.phonebookEnabled&&book.load(s->id,contacts);
 if(!bookReady&&account.phonebookEnabled){if(message.length())message+='\n';message+="Telefonbuchdatei nicht lesbar. Keine vorhandenen Kontakte werden überschrieben.";}
 if(!account.phonebookEnabled){if(message.length())message+='\n';message+="Telefonbuch für dieses Konto ausgeschaltet. In der Kontoverwaltung einschalten.";}
 auto client=accounts.calendarClient(s->id);std::unique_ptr<EventRecord[]> records(new(std::nothrow) EventRecord[32]{});
 const size_t count=records?client.copyEvents(records.get(),32):0;std::vector<EventRecord> events;std::vector<std::unique_ptr<EventDetails>> details;
 for(size_t i=0;i<count;++i){auto detail=std::unique_ptr<EventDetails>(new(std::nothrow) EventDetails{});if(!detail)break;
   if(!client.copyEventDetails(records[i].localId,*detail)){detail->localId=records[i].localId;detailPreview(detail->location,sizeof(detail->location),records[i].location);detailPreview(detail->notes,sizeof(detail->notes),records[i].notes);}
   auto plain=ContactProtocol::plainNotes(detail->notes,detail->notesHtml);detailPreview(detail->notes,sizeof(detail->notes),plain.c_str());detail->notesHtml=0;
   events.push_back(records[i]);details.push_back(std::move(detail));}
 const auto current=accounts.accountInfo(s->id);
 if(current.email!=s->email||!current.enabled||current.phonebookEnabled!=account.phonebookEnabled){contacts.clear();events.clear();details.clear();message="Kontoeinstellung geändert. Ansicht aktualisieren.";}
 Lock lock(s->mutex);if(s->alive){s->contacts=std::move(contacts);s->events=std::move(events);s->details=std::move(details);s->bookReady=bookReady&&current.phonebookEnabled&&current.enabled&&current.email==s->email;
 s->message=message;s->ready=true;}s->busy=false;
}
void queue(Page& page,bool save=false,std::vector<PhonebookContact> local={}){
 if(page.timer){lv_timer_set_period(page.timer,200);lv_timer_reset(page.timer);}
 auto s=page.state;if(s->busy.exchange(true))return;auto* job=new(std::nothrow) Job{s,save,std::move(local)};
 if(!job||!NetworkWorker::instance().submit(load,job,"account-data")){delete job;s->busy=false;lv_label_set_text(page.status,"Dienst beschäftigt. Erneut versuchen.");}
 else lv_label_set_text(page.status,save?"Kontakt wird sicher gespeichert …":"Kontakte und Termine werden geladen …");
}
lv_obj_t* button(lv_obj_t* parent,const char* text,lv_event_cb_t callback,Page* page){auto* b=lv_button_create(parent);SettingsWidgets::navButton(b,false);lv_obj_set_width(b,LV_PCT(100));lv_obj_set_height(b,52);
 auto* label=SettingsWidgets::label(b,text,0);lv_obj_center(label);lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,page);return b;}
String date(int64_t utc){time_t value=time_t(utc);tm local{};localtime_r(&value,&local);char text[40];strftime(text,sizeof(text),"%d.%m.%Y %H:%M",&local);return String(text);}
void show(Page& page,const char* title,const String& value){if(!value.length())return;SettingsWidgets::field(page.detail,title);
 auto* label=SettingsWidgets::label(page.detail,value.c_str(),0);lv_obj_set_width(label,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);}
void saveContact(lv_event_t* event){auto& p=*static_cast<Page*>(lv_event_get_user_data(event));if(!p.bookReady||p.state->busy)return;
 PhonebookContact c;c.id=p.editingId;if(!c.id.length()){char key[48];snprintf(key,sizeof(key),"local-%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());c.id=key;}
 String* fields[]={&c.name,&c.email,&c.phone,&c.mobile,&c.street,&c.houseNumber,&c.postalCode,&c.city,&c.country,&c.notes};
 for(size_t i=0;i<10;++i)*fields[i]=lv_textarea_get_text(p.fields[i]);if(!c.name.length()&&!c.phone.length()&&!c.mobile.length()){lv_label_set_text(p.status,"Name oder Telefonnummer angeben.");return;}
 std::vector<PhonebookContact> local;bool replaced=false;for(const auto& old:p.contacts)if(!old.readOnly){if(old.id==c.id){local.push_back(c);replaced=true;}else local.push_back(old);}if(!replaced)local.push_back(c);queue(p,true,std::move(local));
}
void deleteContact(lv_event_t* event){auto& p=*static_cast<Page*>(lv_event_get_user_data(event));if(!p.bookReady||!p.editingId.length()||p.state->busy)return;
 auto* target=static_cast<lv_obj_t*>(lv_event_get_target(event));if(!lv_obj_has_state(target,LV_STATE_CHECKED)){lv_obj_add_state(target,LV_STATE_CHECKED);lv_label_set_text(lv_obj_get_child(target,0),"Ja, lokalen Kontakt löschen");return;}
 std::vector<PhonebookContact> local;for(const auto& c:p.contacts)if(!c.readOnly&&c.id!=p.editingId)local.push_back(c);queue(p,true,std::move(local));
}
void edit(Page& p,const PhonebookContact& c){lv_obj_clean(p.detail);p.editingId=c.readOnly?String():c.id;
 SettingsWidgets::label(p.detail,c.readOnly?"Anbieter-Kontakt · nur lesend":"Lokalen Kontakt bearbeiten",0,&lv_font_montserrat_24);
 const char* names[]={"Name","E-Mail","Telefon","Mobil","Straße","Hausnummer","Postleitzahl","Ort","Land","Notizen"};
 const String* values[]={&c.name,&c.email,&c.phone,&c.mobile,&c.street,&c.houseNumber,&c.postalCode,&c.city,&c.country,&c.notes};
 const uint32_t limits[]={256,254,96,96,192,32,32,128,96,1024};
 for(size_t i=0;i<10;++i){if(c.readOnly){show(p,names[i],*values[i]);continue;}
   SettingsWidgets::field(p.detail,names[i]);auto* input=lv_textarea_create(p.detail);SettingsWidgets::darkInput(input);lv_obj_set_width(input,LV_PCT(100));lv_obj_set_height(input,i==9?100:52);
   lv_textarea_set_one_line(input,i!=9);lv_textarea_set_max_length(input,limits[i]);lv_textarea_set_text(input,values[i]->c_str());p.fields[i]=input;
   if(i==2||i==3)InputKeyboard::setMode(input,keyboardInput::Mode::Phone);
   if(p.focus)for(auto code:{LV_EVENT_PRESSED,LV_EVENT_SHORT_CLICKED})lv_obj_add_event_cb(input,p.focus,code,p.context);}
 if(!c.readOnly){button(p.detail,"Lokalen Kontakt speichern",saveContact,&p);if(c.id.length())button(p.detail,"Lokalen Kontakt löschen",deleteContact,&p);}
}
void select(lv_event_t* event){auto& p=*static_cast<Page*>(lv_event_get_user_data(event));const size_t index=uintptr_t(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
 if(p.contactsTab){if(index<p.contacts.size())edit(p,p.contacts[index]);return;}if(index>=p.events.size()||index>=p.details.size())return;
 lv_obj_clean(p.detail);const auto& eventRecord=p.events[index];const auto& details=*p.details[index];SettingsWidgets::label(p.detail,eventRecord.title,0,&lv_font_montserrat_24);
 show(p,"Beginn",date(eventRecord.startUtc));show(p,"Ende",date(eventRecord.endUtc));show(p,"Ort / Treffpunkt",details.location);
 show(p,"Straße",details.street);show(p,"Hausnummer",details.houseNumber);show(p,"Postleitzahl",details.postalCode);show(p,"Stadt",details.city);show(p,"Region",details.region);show(p,"Land",details.country);
 show(p,"Telefonnummer",details.phone);show(p,"Notizen",details.notes);if(details.truncated)SettingsWidgets::section(p.detail,"Providerangaben überschreiten die Feldgrenzen; gekürzte Ansicht.");
 SettingsWidgets::section(p.detail,"Anbieter-Termin · nur lesend. Es werden keine Termine in der Cloud geändert.");
}
void list(Page& p){lv_obj_clean(p.list);lv_obj_clean(p.detail);String search=lv_textarea_get_text(p.search);search.toLowerCase();
 size_t shown=0,count=p.contactsTab?p.contacts.size():p.events.size();for(size_t i=0;i<count;++i){String title=p.contactsTab?(p.contacts[i].name.length()?p.contacts[i].name:p.contacts[i].phone):String(p.events[i].title);
   String searchable=p.contactsTab?title+" "+p.contacts[i].email+" "+p.contacts[i].phone+" "+p.contacts[i].mobile:title;searchable.toLowerCase();if(search.length()&&searchable.indexOf(search)<0)continue;
   auto* b=button(p.list,title.c_str(),select,&p);lv_obj_set_user_data(b,reinterpret_cast<void*>(uintptr_t(i)));auto* label=lv_obj_get_child(b,0);lv_obj_set_width(label,LV_PCT(94));lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);++shown;}
 if(!shown)SettingsWidgets::section(p.list,p.contactsTab?"Keine Kontakte. Telefonbuch einschalten, verbinden oder lokal anlegen.":"Keine Termine im aktuellen Kalender-Cache. Kalender freigeben und synchronisieren.");
}
void search(lv_event_t* event){list(*static_cast<Page*>(lv_event_get_user_data(event)));}
void tab(lv_event_t* event){auto& p=*static_cast<Page*>(lv_event_get_user_data(event));p.contactsTab=uintptr_t(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))))==0;list(p);}
void fresh(lv_event_t* event){queue(*static_cast<Page*>(lv_event_get_user_data(event)));}
void add(lv_event_t* event){auto& p=*static_cast<Page*>(lv_event_get_user_data(event));if(!p.bookReady){lv_label_set_text(p.status,"Telefonbuch einschalten und Daten zuerst laden.");return;}p.contactsTab=true;edit(p,PhonebookContact{});}
void update(lv_timer_t* timer){auto& p=*static_cast<Page*>(lv_timer_get_user_data(timer));auto s=p.state;
 lv_timer_set_period(timer,(s->busy.load()||s->ready.load())?200:2000);
 const auto account=CloudAccountService::instance().accountInfo(s->id);
 if(account.email!=s->email||!account.enabled){
    if(!p.contacts.empty()||!p.events.empty()||p.bookReady){p.contacts.clear();p.events.clear();p.details.clear();p.bookReady=false;list(p);lv_label_set_text(p.status,"Konto geändert oder deaktiviert. Ansicht erneut öffnen.");}
    s->ready=false;return;}
 else if(!account.phonebookEnabled&&p.bookReady){p.contacts.clear();p.bookReady=false;list(p);lv_label_set_text(p.status,"Telefonbuch ausgeschaltet.");}
 if(!s->ready.exchange(false))return;
 {Lock lock(s->mutex);if(!account.phonebookEnabled)s->contacts.clear();p.contacts=std::move(s->contacts);p.events=std::move(s->events);p.details=std::move(s->details);p.bookReady=s->bookReady&&account.phonebookEnabled;
 lv_label_set_text(p.status,s->message.length()?s->message.c_str():"Offline-Daten geladen. Kontaktabgleich über die Kontoverwaltung starten.");}list(p);
}
void destroy(lv_event_t* event){auto* p=static_cast<Page*>(lv_event_get_user_data(event));if(lv_event_get_target(event)!=p->root)return;
 p->state->alive=false;if(p->timer)lv_timer_delete(p->timer);delete p;}
}
void AccountDataUi::build(lv_obj_t* parent,const String& id,lv_event_cb_t focus,void* context){
 auto* p=new(std::nothrow) Page{};if(!p)return;p->state=std::make_shared<Snapshot>();if(!p->state->mutex){delete p;SettingsWidgets::section(parent,"Datenansicht nicht verfügbar.");return;}
 p->state->id=id;p->state->email=CloudAccountService::instance().accountInfo(id).email;p->focus=focus;p->context=context;
 p->root=SettingsWidgets::content(parent,false);lv_obj_set_height(p->root,LV_SIZE_CONTENT);lv_obj_set_flex_grow(p->root,0);
 SettingsWidgets::label(p->root,"Telefonbuch & Termine",0,&lv_font_montserrat_24);p->status=SettingsWidgets::section(p->root,"Daten werden geladen …");
 auto* actions=SettingsWidgets::row(p->root,LV_SIZE_CONTENT);for(unsigned i=0;i<2;++i){auto* b=button(actions,i?"Termine":"Telefonbuch",tab,p);lv_obj_set_width(b,LV_PCT(24));lv_obj_set_user_data(b,reinterpret_cast<void*>(uintptr_t(i)));}
 auto* reload=button(actions,"Neu laden",fresh,p);lv_obj_set_width(reload,LV_PCT(24));auto* create=button(actions,"Kontakt anlegen",add,p);lv_obj_set_width(create,LV_PCT(24));
 p->search=lv_textarea_create(p->root);SettingsWidgets::darkInput(p->search);lv_textarea_set_one_line(p->search,true);lv_textarea_set_max_length(p->search,96);lv_textarea_set_placeholder_text(p->search,"Name, E-Mail oder Telefonnummer suchen");lv_obj_set_width(p->search,LV_PCT(100));
 lv_obj_add_event_cb(p->search,search,LV_EVENT_VALUE_CHANGED,p);if(focus)lv_obj_add_event_cb(p->search,focus,LV_EVENT_CLICKED,context);
 auto* body=SettingsWidgets::row(p->root,520);p->list=SettingsWidgets::content(body,true);lv_obj_set_width(p->list,LV_PCT(36));lv_obj_set_flex_grow(p->list,0);
 p->detail=SettingsWidgets::content(body,true);SettingsWidgets::card(p->detail);lv_obj_set_style_pad_all(p->detail,16,0);
 p->timer=lv_timer_create(update,200,p);lv_obj_add_event_cb(p->root,destroy,LV_EVENT_DELETE,p);queue(*p);
}
}
