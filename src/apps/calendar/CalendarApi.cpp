#include "CalendarApi.h"
#include "CalendarService.h"
#include "CalendarPersonOptions.h"
#include "../../services/PhonebookService.h"
#include "../../services/PersonName.h"
#include "../../services/CalendarSyncService.h"
#include "../../services/StorageService.h"
#include "../../services/SecurityService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/ConfigService.h"
#include "../../services/PostalLookupService.h"
#include <memory>
#include <algorithm>
namespace hp {namespace {
String identifier(uint64_t id){char value[17];snprintf(value,sizeof(value),"%08lx%08lx",(unsigned long)(id>>32),(unsigned long)id);return value;}
String identity32(uint32_t id){char value[9];snprintf(value,sizeof(value),"%08lx",(unsigned long)id);return value;}
uint32_t number32(JsonVariantConst value){if(value.is<uint32_t>())return value.as<uint32_t>();const char* s=value.as<const char*>();if(!s||strlen(s)!=8)return 0;uint32_t result=0;for(unsigned i=0;i<8;++i){if(!isxdigit((unsigned char)s[i]))return 0;result=(result<<4)|(s[i]<='9'?s[i]-'0':tolower(s[i])-'a'+10);}return result;}
bool identity(JsonVariantConst value,uint64_t& id){const char* s=value.as<const char*>();id=0;if(!s||strlen(s)!=16)return false;for(unsigned i=0;i<16;++i){if(!isxdigit((unsigned char)s[i]))return false;id=(id<<4)|(s[i]<='9'?s[i]-'0':tolower(s[i])-'a'+10);}return id!=0;}
template<size_t N> bool text(JsonVariantConst source,const char* key,char(&dest)[N]){auto v=source[key];const char* s=v.as<const char*>();if(v.isNull()){dest[0]=0;return true;}if(!s||strlen(s)>=N)return false;strlcpy(dest,s,N);return true;}
// ArduinoJson links const char pointers. These records are destroyed before
// completeCalendarJob serializes the result, so all record text must be owned.
void eventJson(JsonObject out,const EventRecord& e){out["id"]=identifier(e.localId);out["calendar_id"]=identity32(e.calendarId);out["revision"]=e.revision;out["start"]=e.startUtc;out["end"]=e.endUtc;out["title"]=String(e.title);out["location"]=String(e.location);out["all_day"]=bool(e.flags&EventAllDay);out["read_only"]=bool(e.flags&EventReadOnly);out["participants"]=e.participantMask;out["recurring"]=bool(e.flags&EventRecurring);}
void detailsJson(JsonObject out,const EventDetails& d){out["location"]=String(d.location);out["street"]=String(d.street);out["house_number"]=String(d.houseNumber);out["postal_code"]=String(d.postalCode);out["city"]=String(d.city);out["region"]=String(d.region);out["country"]=String(d.country);out["phone"]=String(d.phone);out["notes"]=String(d.notes);out["notes_html"]=d.notesHtml;out["notes_truncated"]=d.truncated;}
bool detailsRead(JsonVariantConst a,EventDetails& d){return text(a,"location",d.location)&&text(a,"street",d.street)&&text(a,"house_number",d.houseNumber)&&text(a,"postal_code",d.postalCode)&&text(a,"city",d.city)&&text(a,"region",d.region)&&text(a,"country",d.country)&&text(a,"phone",d.phone)&&text(a,"notes",d.notes);}
}
void completeCalendarJob(JsonDocument& output, LuaJobService::Result& result) {
 if(!result.ok)return;
 if(output.overflowed()||measureJson(output)>LuaJobService::kResponseBytes){result.ok=false;result.error="calendar_result_limit";return;}
 serializeJson(output,result.body);result.size=result.body.length();result.status=200;
}
void executeCalendarJob(const LuaJobService::Request& request,LuaJobService::Result& result) {
 if(request.cancelled&&request.cancelled->load()){result.cancelled=true;result.error="cancelled";return;}
 JsonDocument args,output;
 if(request.body.length()>LuaJobService::kBodyBytes||deserializeJson(args,request.body,DeserializationOption::NestingLimit(8))){result.error="calendar_json";return;}
 result.ok=executeCalendarOperation(request.context.c_str(),args.as<JsonVariantConst>(),output,result.error,request.cancelled);
 // Discard a result cancelled during synchronous execution, rather than publishing stale data.
 if(request.cancelled&&request.cancelled->load()){result.ok=false;result.cancelled=true;result.error="cancelled";return;}
 completeCalendarJob(output,result);
}
bool executeCalendarOperation(const char* op,JsonVariantConst a,JsonDocument& result,String& error,const std::atomic<bool>* cancelled){
 auto& service=CalendarService::instance();result.clear();
 if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
 if(SecurityService::instance().tabletLocked()){error="tablet_locked";return false;}
 if(!strcmp(op,"postal_lookup"))return lookupPostalCode(a,result,error,cancelled);
 std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
 if(!service.storageHealthy()&&(!service.recoverProfiles()||!service.storageHealthy())){error="profile_commit_pending";return false;}
 if(!strcmp(op,"person_preferences")||!strcmp(op,"person_contacts")){
  if(!SecurityService::instance().settingsAllowed()){error="settings_locked";return false;}
  JsonDocument preferences;if(!calendar::loadPersonOptions(preferences)){error="person_preferences_corrupt";return false;}
  if(!a["date_format"].isNull()){
   SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()){error="settings_locked";return false;}
   const char* format=a["date_format"].as<const char*>();if(!format||(strcmp(format,"iso")&&strcmp(format,"de"))){error="date_format_invalid";return false;}
   preferences["date_format"]=format;if(!calendar::savePersonOptions(preferences)){error="preferences_write_failed";return false;}
  }
  result["date_format"]=String(preferences["date_format"]|"iso");
  if(!strcmp(op,"person_contacts")){
   const int offset=a["offset"]|0;if(offset<0||offset>128){error="contact_offset";return false;}
   LocalPhonebook contacts;if(!PhonebookService::instance().loadLocal(contacts)){error="contact_storage_unavailable";return false;}
   auto rows=result["items"].to<JsonArray>();size_t next=std::min<size_t>(offset,contacts.contacts.size());
   for(;next<contacts.contacts.size()&&next<size_t(offset+32);++next){const auto& c=contacts.contacts[next];auto row=rows.add<JsonObject>();row["id"]=c.id;row["name"]=c.name;}
   result["more"]=next<contacts.contacts.size();result["next_offset"]=next;
  }
 }else if(!strcmp(op,"people")||!strcmp(op,"calendars")){
  service.refreshExternalCalendars();
  std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;service.copyMetadata(people,calendars);
  auto rows=result["items"].to<JsonArray>();
  if(!strcmp(op,"people")){
   JsonDocument preferences;if(!calendar::loadPersonOptions(preferences)){error="person_preferences_corrupt";return false;}
   result["date_format"]=String(preferences["date_format"]|"iso");
   for(const auto& p:people){auto row=rows.add<JsonObject>();row["id"]=identity32(p.id);row["participant_mask"]=service.participantMaskForPersonId(p.id);row["revision"]=p.revision;row["name"]=String(p.displayName);String first=preferences["names"][identity32(p.id)]["first"]|"",last=preferences["names"][identity32(p.id)]["last"]|"";if(!first.length()&&!last.length()){first=p.displayName;last="";}row["firstName"]=first;row["lastName"]=last;row["email"]=String(p.email);row["contact_email"]=String(preferences["emails"][identity32(p.id)]|(String(p.email).endsWith("@calendar.local")?"":p.email));row["color"]=p.colorIndex;row["birthday_year"]=p.birthdayYear;row["birthday_month"]=p.birthdayMonth;row["birthday_day"]=p.birthdayDay;row["contact_id"]=String(preferences["contacts"][identity32(p.id)]|"");row["calendars"].set(preferences["views"][identity32(p.id)]);}
  }
  else for(const auto& c:calendars){auto row=rows.add<JsonObject>();row["id"]=identity32(c.id);row["owner"]=identity32(c.ownerPersonId);row["name"]=String(c.name);row["kind"]=c.kind;row["color"]=c.colorIndex;row["visible"]=bool(c.enabled);row["editable"]=service.canEditCalendar(c.id);}
 }else if(!strcmp(op,"query")){
  CalendarService::Query q;q.cancelled=cancelled;q.fromUtc=a["from"]|int64_t(0);q.toUtc=a["to"]|int64_t(0);
  const int offset=a["offset"]|0,limit=a["limit"]|32,preset=a["preset"]|0;
  if(offset<0||offset>256||limit<1||limit>32||preset<0||preset>2){error="query_argument";return false;}
  q.offset=offset;q.limit=limit;q.preset=preset;q.calendarId=number32(a["calendar_id"]);q.personId=number32(a["person_id"]);
  if(q.personId&&!q.calendarId){JsonDocument options;if(!calendar::loadPersonOptions(options)){error="person_preferences_corrupt";return false;}
   std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;service.copyMetadata(people,calendars);
   for(auto id:options["views"][identity32(q.personId)].as<JsonArrayConst>()){const auto value=number32(id);for(const auto& c:calendars)if(c.id==value&&c.kind!=uint8_t(CalendarKind::BirthdaysVirtual)&&c.kind!=uint8_t(CalendarKind::HolidaysVirtual)&&q.selectedCalendarCount<4)q.selectedCalendars[q.selectedCalendarCount++]=value;}
  }
  if(!a["cursor"].isNull()){if(!a["cursor"].is<JsonObjectConst>()||!identity(a["cursor"]["id"],q.afterId)||!a["cursor"]["start"].is<int64_t>()||offset){error="query_cursor";return false;}q.afterStartUtc=a["cursor"]["start"].as<int64_t>();if(q.afterStartUtc<=0){error="query_cursor";return false;}}
  std::vector<CalendarService::Occurrence> events;bool more=false;if(!service.query(q,events,more,error))return false;
  const bool cloudQueued=CalendarSyncService::instance().requestRange(q.fromUtc,q.toUtc);result["cloud_refresh_queued"]=cloudQueued;
  auto rows=result["items"].to<JsonArray>();for(const auto& e:events){auto row=rows.add<JsonObject>();eventJson(row,e.event);row["occurrence"]=e.originalStartUtc;}
  result["more"]=more;result["next_offset"]=offset+events.size();result["revision"]=service.syncRevision();
  result["cloud_revision"]=CalendarSyncService::instance().dataRevision();
  bool incomplete=false,pending=cloudQueued;for(uint8_t slot=0;slot<5;++slot){const auto info=CalendarSyncService::instance().info(slot);incomplete|=info.truncated;pending|=info.working;}
  result["cloud_incomplete"]=incomplete;
  result["pending"]=pending;
  if(more&&!events.empty()){auto cursor=result["next_cursor"].to<JsonObject>();cursor["id"]=identifier(events.back().event.localId);cursor["start"]=events.back().event.startUtc;}
 }else if(!strcmp(op,"details")||!strcmp(op,"save")||!strcmp(op,"delete")){
  uint64_t id=0;if(!a["id"].isNull()&&!identity(a["id"],id)){error="event_id";return false;}
  if(!strcmp(op,"delete")){if(!id){error="event_id";return false;}return service.deleteEvent(id,a["revision"]|uint32_t(0),a["occurrence"]|int64_t(0),error);}
  auto event=std::unique_ptr<StoredCalendarEvent>(new(std::nothrow) StoredCalendarEvent);if(!event){error="memory";return false;}
  if(!strcmp(op,"details")){if(!service.eventDetails(id,*event,error))return false;}
  else {
   event->event.localId=id;event->event.calendarId=number32(a["calendar_id"]);event->event.startUtc=a["start"]|int64_t(0);event->event.endUtc=a["end"]|int64_t(0);
   event->event.flags=(a["all_day"]|false)?EventAllDay:0;event->event.participantMask=a["participants"]|uint16_t(0);
   if(!text(a,"title",event->event.title)||!detailsRead(a,event->details)){error="event_text_limit";return false;}
   auto rules=a["rules"];const int repeat=rules["repeat"]|0,interval=rules["interval"]|1,count=rules["count"]|0,reminder=rules["reminder_minutes"]|-1;
   if(repeat<0||repeat>4||interval<1||interval>366||count<0||count>10000||reminder<-1||reminder>10080){error="recurrence_argument";return false;}
   event->rules.repeat=calendar::Repeat(repeat);event->rules.interval=interval;event->rules.count=count;event->rules.untilUtc=rules["until"]|int64_t(0);event->rules.reminderMinutes=reminder;
   auto exceptions=rules["excluded"].as<JsonArrayConst>();if(exceptions.size()>32){error="recurrence_exception_limit";return false;}for(auto v:exceptions){if(!v.is<int64_t>()||v.as<int64_t>()<=0){error="recurrence_exception_invalid";return false;}event->rules.excluded[event->rules.excludedCount++]=v.as<int64_t>();}
   if(!service.saveEvent(*event,a["revision"]|uint32_t(0),error))return false;
  }
  auto row=result.to<JsonObject>();eventJson(row,event->event);detailsJson(row,event->details);
  auto rules=result["rules"].to<JsonObject>();rules["repeat"]=uint8_t(event->rules.repeat);rules["interval"]=event->rules.interval;rules["count"]=event->rules.count;rules["until"]=event->rules.untilUtc;rules["reminder_minutes"]=event->rules.reminderMinutes;
  auto exceptions=rules["excluded"].to<JsonArray>();for(unsigned i=0;i<event->rules.excludedCount;++i)exceptions.add(event->rules.excluded[i]);
 }else if(!strcmp(op,"visibility")){
  if(!a["visible"].is<bool>()){error="visibility_argument";return false;}return service.setVisibility(number32(a["calendar_id"]),a["visible"].as<bool>(),error);
 }else if(!strcmp(op,"appearance")){
  const int color=a["color"]|0;if(color<0||color>15){error="color_argument";return false;}return service.setCalendarAppearance(number32(a["calendar_id"]),a["name"]|"",color,error);
 }else if(!strcmp(op,"region")){
  if(!a["state"].isNull()){calendar::Region r;if(!text(a,"state",r.state)){error="region_invalid";return false;}r.assumption=a["assumption"]|false;r.augsburg=a["augsburg"]|false;r.corpusChristiMunicipality=a["corpus_christi_municipality"]|false;if(!service.setRegion(r,error))return false;}
  const auto r=service.region();result["state"]=String(r.state);result["assumption"]=r.assumption;result["augsburg"]=r.augsburg;result["corpus_christi_municipality"]=r.corpusChristiMunicipality;
 }else if(!strcmp(op,"person_save")){
  return service.saveProfile(a,result,error);
 }else if(!strcmp(op,"person_remove")){
  service.refreshExternalCalendars();return service.removeProfile(number32(a["id"]),a["confirmed"]|false,error);
 }else if(!strcmp(op,"user_profile")){
  if(!SecurityService::instance().settingsAllowed()){error="settings_locked";return false;}
  std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;service.copyMetadata(people,calendars);
  const auto id=number32(a["id"]);bool found=false;for(const auto& person:people)if(person.id==id){found=true;result["revision"]=person.revision;}if(!found&&a["contact_id"].isNull()){error="person_not_found";return false;}
  JsonDocument options;if(!calendar::loadPersonOptions(options)){error="person_preferences_corrupt";return false;}
  LocalPhonebook book;if(!PhonebookService::instance().loadLocal(book)){error="contact_storage_unavailable";return false;}
  const String link=a["contact_id"].isNull()?String(options["contacts"][identity32(id)]|""):a["contact_id"].as<String>();result["id"]=identity32(id);result["contact_id"]=link;
  for(const auto& contact:book.contacts)if(contact.id==link)PhonebookService::encodeContact(result["contact"].to<JsonObject>(),contact);
  if(!a["contact_id"].isNull()&&result["contact"].isNull()){error="contact_not_found";return false;}
 }else if(!strcmp(op,"calendar_create")){
  SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()){error="settings_locked";return false;}
  const int kind=a["kind"]|1;if(kind<0||kind>2||!service.upsertCalendar(a["name"]|"",CalendarKind(kind),number32(a["owner"]))){error="calendar_create_failed";return false;}
 }else if(!strcmp(op,"account_person")){
  const int slot=a["slot"]|-1;if(slot<0||slot>=5||a["person_id"].isNull()){error="account_link_argument";return false;}
  const auto person=number32(a["person_id"]);if(!person&&a["person_id"]!=0&&a["person_id"]!="00000000"){error="person_id";return false;}
  return service.linkAccountPerson(slot,person,error);
 }else if(!strcmp(op,"collection_selection")){
  SecurityWriteGuard guard;
  if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
  const int slot=a["slot"]|-1;auto selected=a["items"].as<JsonArrayConst>();
  if(slot<0||slot>=5||!a["items"].is<JsonArrayConst>()||selected.size()>16){error="collection_argument";return false;}
  auto& sync=CalendarSyncService::instance();const auto info=sync.info(slot);auto server=sync.calDavServer(slot);
  if(!server.length())server=String("oauth:")+unsigned(info.provider);
  if(!info.configured||info.working){error="collection_not_ready";return false;}
  const auto known=sync.calendarCollections(String("account-")+slot);
  JsonDocument value;value["server"]=server;value["user"]=info.email;auto items=value["items"].to<JsonArray>();
  for(auto item:selected){const char* id=item.as<const char*>();bool found=false;if(!id){error="collection_argument";return false;}
   for(const auto& collection:known)if(collection.id==id)found=true;
   for(auto prior:items)if(String(prior.as<const char*>())==id){error="collection_duplicate";return false;}
   if(!found){error="collection_unknown";return false;}items.add(id);
  }
  String encoded;serializeJson(value,encoded);if(encoded.length()>4096){error="collection_limit";return false;}
  if(!ConfigService::instance().putString((String("cal_sel_")+slot).c_str(),encoded)){error="collection_write_failed";return false;}
  result["requested"]=sync.sync(slot);
 }else if(!strcmp(op,"sync")||!strcmp(op,"status")){
  service.refreshExternalCalendars();std::vector<PersonRecord> people;std::vector<CalendarRecord> calendars;service.copyMetadata(people,calendars);
  auto rows=result["accounts"].to<JsonArray>();for(uint8_t slot=0;slot<5;++slot){auto info=CalendarSyncService::instance().info(slot);if(!info.configured)continue;
   auto row=rows.add<JsonObject>();row["slot"]=slot;row["email"]=info.email;row["message"]=info.message;row["working"]=info.working;row["connected"]=info.connected;row["last_sync_ms"]=info.lastSyncMs;row["events"]=info.eventCount;row["truncated"]=info.truncated;
   row["person_id"]="00000000";for(const auto& c:calendars)if(c.id==0x80000000u+slot)row["person_id"]=identity32(c.ownerPersonId);
   auto& sync=CalendarSyncService::instance();auto server=sync.calDavServer(slot);if(!server.length())server=String("oauth:")+unsigned(info.provider);row["collection_selection_supported"]=true;
   JsonDocument selected;const auto saved=ConfigService::instance().getString((String("cal_sel_")+slot).c_str(),"");
   const bool explicitSelection=saved.length()<=4096&&!deserializeJson(selected,saved)&&selected["server"]==server&&selected["user"]==info.email&&selected["items"].is<JsonArray>();
   auto collections=row["collections"].to<JsonArray>();for(const auto& collection:sync.calendarCollections(String("account-")+slot)){
    bool enabled=!explicitSelection;for(auto id:selected["items"].as<JsonArrayConst>())if(String(id.as<const char*>())==collection.id)enabled=true;
    auto item=collections.add<JsonObject>();item["id"]=collection.id;item["name"]=collection.displayName;item["selected"]=enabled;item["read_only"]=collection.readOnly;
   }
   if(!strcmp(op,"sync"))row["requested"]=CalendarSyncService::instance().sync(slot);
  }
  result["storage_healthy"]=service.storageHealthy();result["sd_available"]=StorageService::instance().sdAvailable();result["internal_event_limit"]=CalendarEventStore::kInternalLimit;
 }else {error="calendar_operation_unknown";return false;}
 return true;
}
}
