#include "MusicApp.h"
#include "MusicPlayerService.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/InputKeyboard.h"
#include "../../ui/UiShell.h"
#include "../../services/FileBrowserService.h"
#include "../../ui/NativeAppIcon.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
namespace hp {
struct MusicPage {
 lv_obj_t *host=nullptr,*root=nullptr,*tabs=nullptr,*body=nullptr,*art=nullptr,*status=nullptr,*now=nullptr,*keyboard=nullptr,*name=nullptr,*url=nullptr,*folder=nullptr,*search=nullptr;
 lv_obj_t *play=nullptr,*heart=nullptr,*volume=nullptr,*pages=nullptr;lv_timer_t* timer=nullptr;
 lv_obj_t *rows[6]{},*names[6]{},*sources[6]{},*hearts[6]{};std::vector<size_t> shown;
 unsigned tab=0,offset=0;int action=0;uint32_t revision=UINT32_MAX;bool redraw=true,fullscreen=false,sortDate=false;
 String album,current,query,prefsStamp;std::vector<String> albums,stationUrls,stationNames,playlistNames,playlistUris;
};
namespace {
MusicPage& self(lv_event_t* e){return *static_cast<MusicPage*>(lv_event_get_user_data(e));}
void bubble(lv_obj_t* o){lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)bubble(lv_obj_get_child(o,i));}
void clicked(lv_event_t* e){self(e).action=int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e)))));}
lv_obj_t* button(lv_obj_t* parent,const char* text,int action,MusicPage& p){auto* b=lv_button_create(parent);UiTheme::instance().styleCard(b);lv_obj_set_height(b,44);lv_obj_set_user_data(b,reinterpret_cast<void*>(intptr_t(action)));lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,&p);auto* l=SettingsWidgets::label(b,text,0);lv_obj_center(l);return b;}
void text(lv_obj_t* label,const String& value){if(value!=lv_label_get_text(label))lv_label_set_text(label,value.c_str());}
void hideKeyboard(MusicPage& p){InputKeyboard::hide(p.keyboard);}
void focus(lv_event_t* e){auto& p=self(e);InputKeyboard::bind(p.keyboard,static_cast<lv_obj_t*>(lv_event_get_target(e)));lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p.keyboard);}
lv_obj_t* input(lv_obj_t* parent,const char* placeholder,MusicPage& p){auto* t=lv_textarea_create(parent);lv_textarea_set_one_line(t,true);lv_textarea_set_max_length(t,512);lv_textarea_set_placeholder_text(t,placeholder);lv_obj_set_width(t,LV_PCT(100));UiTheme::instance().styleInput(t);lv_obj_add_event_cb(t,focus,LV_EVENT_FOCUSED,&p);return t;}
String albumFor(const String& uri){const int colon=uri.indexOf(":/"),slash=uri.lastIndexOf('/');String album=slash>colon+1?uri.substring(colon+2,slash):String();
 if(album=="Music"||album=="Musik")return "Ohne Album";if(album.startsWith("Music/")||album.startsWith("Musik/"))album.remove(0,6);return album.length()?album:String("Ohne Album");}
void prefs(MusicPage& p,JsonDocument& d){deserializeJson(d,MusicPlayerService::instance().preferences());}
bool save(JsonDocument& d){String json;serializeJson(d,json);return MusicPlayerService::instance().saveOptions(json);}
void render(MusicPage& p){
 hideKeyboard(p);lv_obj_clean(p.body);p.name=p.url=p.folder=p.search=nullptr;p.shown.clear();p.albums.clear();p.stationUrls.clear();p.stationNames.clear();p.playlistNames.clear();p.playlistUris.clear();
 auto& service=MusicPlayerService::instance();const auto state=service.snapshot();JsonDocument d;prefs(p,d);
 auto favorite=[&d](const String& uri){for(JsonVariant v:d["favorites"].as<JsonArray>())if(v.as<String>()==uri)return true;return false;};
 if(p.tab==4){
  SettingsWidgets::label(p.body,"Internetradio hinzufügen · direkte MP3-/WAV-Streamadresse",0);p.name=input(p.body,"Sender- oder Listenname",p);p.url=input(p.body,"https://… · kein Spotify, AAC oder HLS",p);auto* create=SettingsWidgets::row(p.body,48);button(create,"Sender speichern",40,p);button(create,"Leere Liste anlegen",44,p);
  SettingsWidgets::label(p.body,"Musikordner · SD, USB oder eingerichtetes NAS",0);p.folder=input(p.body,"z. B. usb:/Musik oder nas1:/Music",p);button(p.body,"Ordner hinzufügen",41,p);
  unsigned folderIndex=0;for(JsonVariant v:d["folders"].as<JsonArray>()){auto* row=SettingsWidgets::row(p.body,48);SettingsWidgets::label(row,v.as<const char*>(),0);button(row,"Entfernen",400+folderIndex++,p);}
  auto* row=SettingsWidgets::row(p.body,52);button(row,state.shuffle?"Zufall: Ein":"Zufall: Aus",42,p);button(row,state.repeat?"Wiederholen: Ein":"Wiederholen: Aus",43,p);
  SettingsWidgets::label(p.body,"Automatisch: /Musik und /Music auf SD/USB/NAS. Max. 256 Titel.\nBibliothek aktualisieren nur bei gestoppter Wiedergabe. Änderungen werden sicher gespeichert.",0);
 }else {
  auto* top=SettingsWidgets::row(p.body,48);p.search=input(top,"Suchen …",p);lv_obj_set_width(p.search,LV_PCT(65));lv_textarea_set_text(p.search,p.query.c_str());button(top,LV_SYMBOL_LEFT,10,p);p.pages=SettingsWidgets::label(top,"",0);button(top,LV_SYMBOL_RIGHT,11,p);
  if(p.tab==5){for(JsonVariant v:d["playlists"].as<JsonArray>()){p.playlistNames.push_back(v["name"].as<String>());if(v["name"].as<String>()==p.album)for(JsonVariant uri:v["uris"].as<JsonArray>())p.playlistUris.push_back(uri.as<String>());}}
  else if(p.tab==3){for(JsonVariant v:d["stations"].as<JsonArray>()){p.stationUrls.push_back(v["url"].as<String>());p.stationNames.push_back(v["name"].as<String>());}}
  else for(size_t i=0;i<state.count;++i){MusicTrack t;if(!service.track(i,t))continue;const String album=albumFor(t.path);
   if(p.tab==2&&!p.album.length()){if(std::find(p.albums.begin(),p.albums.end(),album)==p.albums.end())p.albums.push_back(album);continue;}
   String title=t.title,q=p.query;title.toLowerCase();q.toLowerCase();if(q.length()&&title.indexOf(q)<0)continue;
   if(p.tab==1&&!favorite(t.path))continue;if(p.tab==2&&album!=p.album)continue;p.shown.push_back(i);
  }
  if(p.tab==1){for(JsonVariant value:d["favorites"].as<JsonArray>()){const String uri=value.as<String>();if(!uri.startsWith("http://")&&!uri.startsWith("https://"))continue;String title=uri;for(JsonVariant station:d["stations"].as<JsonArray>())if(station["url"].as<String>()==uri){title=station["name"].as<String>();break;}String folded=title,q=p.query;folded.toLowerCase();q.toLowerCase();if(q.length()&&folded.indexOf(q)<0)continue;p.stationUrls.push_back(uri);p.stationNames.push_back(title);}}
  if(p.sortDate)std::stable_sort(p.shown.begin(),p.shown.end(),[&service](size_t a,size_t b){MusicTrack x,y;return service.track(a,x)&&service.track(b,y)&&x.modified>y.modified;});
  const size_t count=p.tab==5?(p.album.length()?p.playlistUris.size():p.playlistNames.size()):p.tab==3?p.stationUrls.size():(p.tab==2&&!p.album.length()?p.albums.size():p.shown.size()+(p.tab==1?p.stationUrls.size():0));if(p.offset>=count)p.offset=0;
  text(p.pages,String(count?1+p.offset/6:0)+" / "+String((count+5)/6));
  for(unsigned row=0;row<6;++row){const size_t index=p.offset+row;p.rows[row]=SettingsWidgets::row(p.body,48);lv_obj_set_width(p.rows[row],LV_PCT(100));
   if(index>=count){if(row==0)SettingsWidgets::label(p.rows[row],state.scanning?"Bibliothek wird gelesen …":"Keine Einträge. Musik auf SD/USB ablegen oder einen Sender hinzufügen.",0);continue;}
   String title,subtitle,uri;
   if(p.tab==5){if(p.album.length()){uri=p.playlistUris[index];title=uri.substring(uri.lastIndexOf('/')+1);subtitle=p.album;}else{title=p.playlistNames[index];subtitle="Wiedergabeliste";}}
   else if(p.tab==3||(p.tab==1&&index>=p.shown.size())){const size_t station=p.tab==3?index:index-p.shown.size();title=p.stationNames[station];uri=p.stationUrls[station];subtitle="Internetradio";}
   else if(p.tab==2&&!p.album.length()){title=p.albums[index];subtitle="Album · SD/USB/NAS gemeinsam";}
   else {MusicTrack t;service.track(p.shown[index],t);title=t.title;uri=t.path;subtitle=uri.substring(0,uri.indexOf(':'))+" · "+albumFor(uri);}
   auto* b=button(p.rows[row],title.c_str(),100+row,p);lv_obj_set_width(b,LV_PCT(62));auto* l=lv_obj_get_child(b,0);lv_obj_set_width(l,LV_PCT(100));lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
   auto* detail=SettingsWidgets::label(p.rows[row],subtitle.c_str(),0);lv_obj_set_width(detail,LV_PCT(22));lv_label_set_long_mode(detail,LV_LABEL_LONG_DOT);
   if(uri.length()){auto* b=button(p.rows[row],"",200+row,p);lv_obj_set_width(b,44);auto* heart=createAppIcon(b,favorite(uri)?"@heart":"@heart_outline",28,UiTheme::instance().accent());lv_obj_center(heart);}
   if(p.tab==3||p.tab==5)button(p.rows[row],LV_SYMBOL_CLOSE,300+row,p);
  }
 }
 bubble(p.body);p.redraw=false;
}
void poll(lv_timer_t* timer){auto& p=*static_cast<MusicPage*>(lv_timer_get_user_data(timer));auto& service=MusicPlayerService::instance();const int action=p.action;p.action=0;
 if(action>=1&&action<=6){p.tab=action-1;p.offset=0;p.album="";p.redraw=true;}
 else if(action==10){if(p.offset>=6)p.offset-=6;else if(p.album.length())p.album="";p.redraw=true;}
 else if(action==11){p.offset+=6;p.redraw=true;}
 else if(action==12){if(!service.rescan())text(p.status,"Bitte Wiedergabe stoppen oder laufenden Auftrag abwarten.");}
 else if(action==13){p.sortDate=!p.sortDate;p.redraw=true;}
 else if(action==20)service.command(MediaCommand::Previous);else if(action==21)service.command(MediaCommand::PlayPause);else if(action==22)service.command(MediaCommand::Next);else if(action==23)service.command(MediaCommand::Stop);
 else if(action==24){service.command(MediaCommand::VolumeDown);}else if(action==25){service.command(MediaCommand::VolumeUp);}
 else if(action==26){p.fullscreen=!p.fullscreen;hideKeyboard(p);UiShell::instance().setAppFullscreen(p.host,p.fullscreen);if(p.fullscreen){lv_obj_add_flag(p.tabs,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(p.body,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(p.art,LV_OBJ_FLAG_HIDDEN);}else{lv_obj_remove_flag(p.tabs,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(p.body,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(p.art,LV_OBJ_FLAG_HIDDEN);}lv_obj_set_style_text_font(p.now,fontWithGermanFallback(p.fullscreen?&lv_font_montserrat_32:&lv_font_montserrat_18),0);}
 else if(action==27){JsonDocument d;prefs(p,d);bool ok=false;for(JsonVariant list:d["playlists"].as<JsonArray>())if(list["name"].as<String>()==p.album&&p.current.length()&&list["uris"].size()<32){auto uris=list["uris"].is<JsonArray>()?list["uris"].as<JsonArray>():list["uris"].to<JsonArray>();bool exists=false;for(JsonVariant uri:uris)if(uri.as<String>()==p.current)exists=true;if(!exists)uris.add(p.current);ok=save(d);break;}text(p.status,ok?"Titel wird zur Liste hinzugefügt.":"Zuerst einen Titel und unter Listen eine Liste auswählen.");}
 else if(action>=100&&action<106){const size_t index=p.offset+action-100;
  if(p.tab==5){if(!p.album.length()){if(index<p.playlistNames.size()){p.album=p.playlistNames[index];p.offset=0;p.redraw=true;}}else if(index<p.playlistUris.size()){p.current=p.playlistUris[index];service.playQueue(p.playlistUris,index);}}
  else if(p.tab==2&&!p.album.length()){if(index<p.albums.size()){p.album=p.albums[index];p.offset=0;p.redraw=true;}}
  else if(p.tab==3||(p.tab==1&&index>=p.shown.size())){const size_t station=p.tab==3?index:index-p.shown.size();if(station<p.stationUrls.size()){p.current=p.stationUrls[station];if(!service.playUri(p.current,p.stationNames[station]))text(p.status,"Wiedergabeauftrag noch beschäftigt.");}}
  else if(index<p.shown.size()){MusicTrack t;if(service.track(p.shown[index],t)){p.current=t.path;std::vector<String> queue;const size_t start=(index/32)*32;for(size_t i=start;i<std::min(p.shown.size(),start+32);++i){MusicTrack item;if(service.track(p.shown[i],item))queue.push_back(item.path);}if(!service.playQueue(queue,index-start))text(p.status,"Wiedergabeauftrag noch beschäftigt.");}}
 }else if(action>=200&&action<206){const size_t i=p.offset+action-200;String uri;if(p.tab==5){if(i<p.playlistUris.size())uri=p.playlistUris[i];}else if(p.tab==3||(p.tab==1&&i>=p.shown.size())){const size_t station=p.tab==3?i:i-p.shown.size();if(station<p.stationUrls.size())uri=p.stationUrls[station];}else if(i<p.shown.size()){MusicTrack t;if(service.track(p.shown[i],t))uri=t.path;}if(uri.length()&&!service.toggleFavorite(uri))text(p.status,"Favorit konnte nicht gespeichert werden.");}
 else if(action>=300&&action<306){JsonDocument d;prefs(p,d);const size_t i=p.offset+action-300;if(p.tab==5){if(!p.album.length()){if(i<d["playlists"].size()){d["playlists"].as<JsonArray>().remove(i);save(d);}}else for(JsonVariant list:d["playlists"].as<JsonArray>())if(list["name"].as<String>()==p.album&&i<list["uris"].size()){list["uris"].as<JsonArray>().remove(i);save(d);break;}}else if(i<d["stations"].size()){d["stations"].as<JsonArray>().remove(i);save(d);}}
 else if(action>=400&&action<432){JsonDocument d;prefs(p,d);const size_t i=action-400;if(i<d["folders"].size()){d["folders"].as<JsonArray>().remove(i);save(d);}}
 else if(action>=40&&action<=44){JsonDocument d;prefs(p,d);bool ok=false;
  if(action==44&&p.name){const String name=lv_textarea_get_text(p.name);bool duplicate=false;for(JsonVariant list:d["playlists"].as<JsonArray>())if(list["name"].as<String>()==name)duplicate=true;if(name.length()&&name.length()<=64&&!duplicate&&d["playlists"].size()<8){auto lists=d["playlists"].is<JsonArray>()?d["playlists"].as<JsonArray>():d["playlists"].to<JsonArray>();auto list=lists.add<JsonObject>();list["name"]=name;list["uris"].to<JsonArray>();ok=save(d);}}
  else if(action==40&&p.name&&p.url){const String name=lv_textarea_get_text(p.name),url=lv_textarea_get_text(p.url);if(name.length()&&url.length()<=512&&(url.startsWith("https://")||url.startsWith("http://"))&&d["stations"].size()<32){auto stations=d["stations"].is<JsonArray>()?d["stations"].as<JsonArray>():d["stations"].to<JsonArray>();auto s=stations.add<JsonObject>();s["name"]=name;s["url"]=url;ok=save(d);}}
  else if(action==41&&p.folder){const String uri=lv_textarea_get_text(p.folder);if(FileBrowserService::validUri(uri)&&d["folders"].size()<8){auto folders=d["folders"].is<JsonArray>()?d["folders"].as<JsonArray>():d["folders"].to<JsonArray>();folders.add(uri);ok=save(d);}}
  else if(action==42||action==43){const char* key=action==42?"shuffle":"repeat";d[key]=!bool(d[key]|false);ok=save(d);}
  text(p.status,ok?"Änderung wird gespeichert …":"Ungültige Eingabe oder Auftrag noch beschäftigt.");
 }
 if(p.search){const String q=lv_textarea_get_text(p.search);if(q!=p.query&&lv_obj_has_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN)){p.query=q;p.offset=0;p.redraw=true;}}
 const auto s=service.snapshot();if(s.revision!=p.revision){const bool initial=p.revision==UINT32_MAX;p.revision=s.revision;
  p.current=service.playingUri();const String settings=service.preferences();if(settings!=p.prefsStamp){p.prefsStamp=settings;p.redraw=true;}
  text(p.now,String(s.title[0]?s.title:"Musik")+" · "+s.subtitle+" · "+String(s.volume)+" %");
  const char* state=s.state==MusicState::Playing?"Wiedergabe":s.state==MusicState::Paused?"Pausiert":s.state==MusicState::Loading?"Verbinden …":s.state==MusicState::Error?s.error:"Bereit";
  text(p.status,s.scanning?"Bibliothek wird eingelesen …":state);
  // Settings/favorite changes and completed scans require only their own page.
  if(initial||(!s.scanning&&s.state==MusicState::Idle))p.redraw=true;
 }
 if(p.redraw&&!p.fullscreen&&lv_obj_has_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN))render(p);
}
}
void MusicApp::open(lv_obj_t* parent){close();page_=new(std::nothrow) MusicPage;if(!page_)return;auto& p=*page_;p.host=parent;
 p.root=lv_obj_create(parent);lv_obj_set_size(p.root,LV_PCT(100),LV_PCT(100));UiTheme::instance().styleScreen(p.root);lv_obj_set_flex_flow(p.root,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(p.root,8,0);
 p.tabs=SettingsWidgets::row(p.root,52);const char* tabs[]={"Musik","Favoriten","Alben","Radio","Einstellungen","Listen"};for(int i=0;i<6;++i)button(p.tabs,tabs[i],i+1,p);button(p.tabs,LV_SYMBOL_REFRESH,12,p);button(p.tabs,LV_SYMBOL_IMAGE,26,p);button(p.tabs,"Name / Datum",13,p);
 lv_obj_set_flex_flow(p.tabs,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_height(p.tabs,LV_SIZE_CONTENT);
 p.art=lv_obj_create(p.root);UiTheme::instance().styleScreen(p.art);lv_obj_set_width(p.art,LV_PCT(100));lv_obj_set_flex_grow(p.art,1);auto* icon=createAppIcon(p.art,"@music",240,UiTheme::instance().accent());lv_obj_center(icon);auto* exit=button(p.art,LV_SYMBOL_CLOSE,26,p);lv_obj_align(exit,LV_ALIGN_TOP_RIGHT,0,0);lv_obj_add_flag(p.art,LV_OBJ_FLAG_HIDDEN);
 p.now=SettingsWidgets::label(p.root,"Musik",0);lv_obj_set_width(p.now,LV_PCT(100));lv_label_set_long_mode(p.now,LV_LABEL_LONG_DOT);
 auto* controls=SettingsWidgets::row(p.root,52);button(controls,LV_SYMBOL_PREV,20,p);button(controls,LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE,21,p);button(controls,LV_SYMBOL_NEXT,22,p);button(controls,LV_SYMBOL_STOP,23,p);button(controls,"−",24,p);button(controls,"+",25,p);button(controls,"Zur Liste",27,p);
 p.body=lv_obj_create(p.root);UiTheme::instance().styleScreen(p.body);lv_obj_set_width(p.body,LV_PCT(100));lv_obj_set_flex_grow(p.body,1);lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(p.body,4,0);
 p.status=SettingsWidgets::label(p.root,"Bibliothek wird vorbereitet …",0);lv_obj_set_width(p.status,LV_PCT(100));lv_label_set_long_mode(p.status,LV_LABEL_LONG_DOT);
 p.keyboard=lv_keyboard_create(p.root);lv_obj_set_size(p.keyboard,LV_PCT(100),240);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_FLOATING);lv_obj_align(p.keyboard,LV_ALIGN_BOTTOM_MID,0,0);InputKeyboard::prepare(p.keyboard);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_event_cb(p.keyboard,[](lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL)hideKeyboard(self(e));},LV_EVENT_ALL,&p);
 lv_obj_add_event_cb(p.root,[](lv_event_t* e){auto& p=self(e);for(auto* target=static_cast<lv_obj_t*>(lv_event_get_target(e));target;target=lv_obj_get_parent(target))if(target==p.keyboard||lv_obj_check_type(target,&lv_textarea_class))return;hideKeyboard(p);},LV_EVENT_PRESSED,&p);
 bubble(p.root);p.timer=lv_timer_create(poll,250,&p);MusicPlayerService::instance().rescan();
}
void MusicApp::close(){if(!page_)return;auto* p=page_;page_=nullptr;if(p->timer)lv_timer_delete(p->timer);if(p->fullscreen)UiShell::instance().setAppFullscreen(p->host,false);hideKeyboard(*p);lv_obj_delete(p->root);delete p;}
}
