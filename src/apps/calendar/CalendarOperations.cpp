#include "CalendarService.h"
#include "../../core/PsramArray.h"
#include "../../services/StorageService.h"
#include "../../services/SecurityService.h"
#include "../../services/ConfigService.h"
#include "../../services/CalendarSyncService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/PhonebookService.h"
#include "ContactBirthday.h"
#include "CalendarPersonOptions.h"
#include <algorithm>
#include <memory>
#include <ArduinoJson.h>
namespace hp {namespace {
bool ordered(const CalendarService::Occurrence& a,const CalendarService::Occurrence& b){return a.event.startUtc!=b.event.startUtc?a.event.startUtc<b.event.startUtc:a.event.localId<b.event.localId;}
String accountScope(uint8_t slot){auto& sync=CalendarSyncService::instance();const auto info=sync.info(slot);const auto server=sync.calDavServer(slot);return server.length()?server:String("oauth:")+unsigned(info.provider);}
uint32_t linkedPerson(uint8_t slot,const String& email,const String& scope){
 JsonDocument record;const auto saved=ConfigService::instance().getString((String("cal_user_")+slot).c_str(),"");
 if(saved.length()>1024||deserializeJson(record,saved)||record["email"]!=email||record["scope"]!=scope)return 0;
 return record["person"]|uint32_t(0);
}
}
void CalendarService::copyMetadata(std::vector<PersonRecord>& people,std::vector<CalendarRecord>& calendars)const{
 std::lock_guard<std::recursive_mutex> lock(mutex_);people.clear();for(size_t i=0;i<personCount_;++i)if(persons_[i].flags&1)people.push_back(persons_[i]);calendars.assign(calendars_,calendars_+calendarCount_);
}
void CalendarService::refreshExternalCalendars(){
 const auto accounts=CalendarSyncService::instance().accounts();std::lock_guard<std::recursive_mutex> lock(mutex_);if(!storageHealthy_)return;
 for(const auto& account:accounts){if(!account.id.startsWith("account-"))continue;const unsigned slot=account.id.substring(8).toInt();if(slot>=5)continue;
  const auto owner=linkedPerson(slot,CalendarSyncService::instance().info(slot).email,accountScope(slot));
  const uint32_t validOwner=personIndexById(owner)>=0?owner:0;
  const uint32_t id=0x80000000u+slot;bool found=false;for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==id){found=true;if(calendars_[i].ownerPersonId!=validOwner){calendars_[i].ownerPersonId=validOwner;bumpRevision();}}
  if(found||calendarCount_>=HOMEPANEL_CALENDAR_MAX_CALENDARS)continue;
  auto& c=calendars_[calendarCount_++];c={};c.id=id;c.kind=uint8_t(CalendarKind::External);c.provider=uint8_t(CalendarProvider::GenericCalDav);c.enabled=1;
  strlcpy(c.name,account.email.c_str(),sizeof(c.name));strlcpy(c.remoteAccountId,account.id.c_str(),sizeof(c.remoteAccountId));
  if(!saveCalendars()){--calendarCount_;return;}c.ownerPersonId=validOwner;bumpRevision();
 }
}
bool CalendarService::linkAccountPerson(uint8_t slot,uint32_t personId,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;
 if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 if(slot>=5){error="account_invalid";return false;}
 const auto info=CalendarSyncService::instance().info(slot);
 if(!info.configured||info.working){error="account_not_ready";return false;}
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!storageHealthy_){error="calendar_storage_integrity";return false;}
 if(personId&&personIndexById(personId)<0){error="person_not_found";return false;}
 JsonDocument record;record["person"]=personId;record["email"]=info.email;record["scope"]=accountScope(slot);
 String saved;serializeJson(record,saved);
 if(saved.length()>1024||!ConfigService::instance().putString((String("cal_user_")+slot).c_str(),saved)){error="account_link_write_failed";return false;}
 refreshExternalCalendars();bumpRevision();return true;
}
calendar::Region CalendarService::region()const{
 calendar::Region result;auto& c=ConfigService::instance();const String state=c.getString("cal_region","BY");
 const String saved=c.getString("cal_region_v1","");char code[3]{};unsigned a=0,b=0,d=0;
 if(sscanf(saved.c_str(),"%2[A-Z],%u,%u,%u",code,&a,&b,&d)==4&&calendar::validRegion(code)&&a<=1&&b<=1&&d<=1){strlcpy(result.state,code,sizeof(result.state));result.assumption=a;result.augsburg=b;result.corpusChristiMunicipality=d;return result;}
 if(calendar::validRegion(state.c_str()))strlcpy(result.state,state.c_str(),sizeof(result.state));
 result.assumption=c.getBool("cal_assump",false);result.augsburg=c.getBool("cal_augsburg",false);result.corpusChristiMunicipality=c.getBool("cal_corpus",false);return result;
}
bool CalendarService::setRegion(const calendar::Region& region,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 if(!calendar::validRegion(region.state)){error="region_invalid";return false;}
 // Single config record avoids half-saved municipality/state combinations.
 char value[32];snprintf(value,sizeof(value),"%s,%u,%u,%u",region.state,region.assumption,region.augsburg,region.corpusChristiMunicipality);
 if(!ConfigService::instance().putString("cal_region_v1",value)){error="region_write_failed";return false;}bumpRevision();return true;
}
bool CalendarService::canEditCalendar(uint32_t id)const{
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;
 std::lock_guard<std::recursive_mutex> lock(mutex_);if(SecurityService::instance().tabletLocked())return false;
 for(size_t i=0;i<calendarCount_;++i){const auto& c=calendars_[i];if(c.id!=id)continue;
  if(c.provider!=uint8_t(CalendarProvider::LocalHomePanel)||c.kind==uint8_t(CalendarKind::BirthdaysVirtual)||c.kind==uint8_t(CalendarKind::HolidaysVirtual))return false;
  // Choosing a person is NOT authentication. Private edits need native admin PIN.
  return c.kind!=uint8_t(CalendarKind::Personal)||(SecurityService::instance().enabled(PinKind::Settings)&&SecurityService::instance().settingsAllowed());
 }return false;
}
bool CalendarService::setVisibility(uint32_t id,bool visible,String& error){
 std::lock_guard<std::recursive_mutex> lock(mutex_);if(!storageHealthy_||SecurityService::instance().tabletLocked()){error="calendar_unavailable";return false;}
 for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==id){auto prior=calendars_[i];if(prior.enabled==visible)return true;
  calendars_[i].enabled=visible;++calendars_[i].revision;if(!saveCalendars()){calendars_[i]=prior;error="calendar_write_failed";return false;}bumpRevision();return true;}
 error="calendar_not_found";return false;
}
bool CalendarService::setCalendarAppearance(uint32_t id,const char* name,uint8_t color,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!storageHealthy_||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){error="settings_locked";return false;}
 if(!name||!name[0]||strlen(name)>=sizeof(CalendarRecord::name)||color>15){error="calendar_argument";return false;}
 for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==id){auto prior=calendars_[i];strlcpy(calendars_[i].name,name,sizeof(calendars_[i].name));calendars_[i].colorIndex=color;++calendars_[i].revision;
  if(!saveCalendars()){calendars_[i]=prior;error="calendar_write_failed";return false;}bumpRevision();return true;}
 error="calendar_not_found";return false;
}
bool CalendarService::eventDetails(uint64_t id,StoredCalendarEvent& out,String& error){
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 auto details=std::unique_ptr<EventDetails>(new(std::nothrow) EventDetails);
 std::unique_ptr<EventRecord[],decltype(&heap_caps_free)> remote(allocatePsramArray<EventRecord>(kCloudCalendarEvents),heap_caps_free);
 if(!details||!remote){error="memory";return false;}
 for(uint8_t slot=0;slot<5;++slot)if(CalendarSyncService::instance().copyEventDetails(slot,id,*details)){
  const auto count=CalendarSyncService::instance().copyEvents(slot,remote.get(),kCloudCalendarEvents);for(size_t i=0;i<count;++i)if(remote[i].localId==id){out={};out.event=remote[i];out.details=*details;out.seal();return true;}
 }
 bool legacyRecord=false;if(!CalendarEventStore::read(id,out,error,&legacyRecord))return false;
 // Keep all legacy addresses/notes, not merely the preview in events.bin.
 auto legacy=std::unique_ptr<EventDetails>(new(std::nothrow) EventDetails);if(legacyRecord&&legacy&&loadEventDetails(id,*legacy))out.details=*legacy;
 out.seal();return !(out.event.flags&EventDeleted);
}
bool CalendarService::saveEvent(StoredCalendarEvent& value,uint32_t expected,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!storageHealthy_||!canEditCalendar(value.event.calendarId)){error="calendar_read_only_or_locked";return false;}
 if(value.event.flags&(EventReadOnly|EventExternal|EventDeleted)){error="event_read_only";return false;}
 for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==value.event.calendarId)value.event.ownerPersonId=calendars_[i].ownerPersonId;
 uint16_t activeMask=0;for(size_t i=0;i<personCount_;++i)if(persons_[i].flags&1)activeMask|=uint16_t(1u<<i);
 if(value.event.participantMask&~activeMask){error="participants_invalid";return false;}
 if(!value.event.title[0]||value.event.endUtc<=value.event.startUtc||value.event.endUtc-value.event.startUtc>366LL*86400||!value.rules.valid()){error="event_argument";return false;}
 auto old=std::unique_ptr<StoredCalendarEvent>(new(std::nothrow) StoredCalendarEvent);if(!old){error="memory";return false;}
 if(value.event.localId){if(!CalendarEventStore::read(value.event.localId,*old,error))return false;
  if(old->event.revision!=expected){error="revision_conflict";return false;}
  if(!canEditCalendar(old->event.calendarId)||(old->event.flags&(EventReadOnly|EventExternal|EventDeleted))){error="event_read_only";return false;}
  value.event.revision=old->event.revision+1;if(!value.event.revision){error="revision_limit";return false;}
  strlcpy(value.event.uid,old->event.uid,sizeof(value.event.uid));
 }else {
  if(expected){error="revision_conflict";return false;}bool unique=false;
  for(unsigned tries=0;tries<16&&!unique;++tries){value.event.localId=nextLocalEventId();String probe;
   if(!CalendarEventStore::read(value.event.localId,*old,probe)){if(probe=="event_not_found")unique=true;else {error=probe;return false;}}
  }
  if(!unique){error="event_identity_collision";return false;}value.event.revision=1;
 }
 value.details.localId=value.event.localId;
 if(!value.event.uid[0])snprintf(value.event.uid,sizeof(value.event.uid),"hp-%08lx%08lx@homepanel.local",(unsigned long)(value.event.localId>>32),(unsigned long)value.event.localId);
 value.event.flags=value.rules.repeat==calendar::Repeat::None?value.event.flags&~EventRecurring:value.event.flags|EventRecurring;
 detailPreview(value.event.location,sizeof(value.event.location),value.details.location);detailPreview(value.event.notes,sizeof(value.event.notes),value.details.notes);value.seal();
 if(!CalendarEventStore::write(value,error))return false;bumpRevision();return true;
}
bool CalendarService::deleteEvent(uint64_t id,uint32_t expected,int64_t occurrence,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());SecurityWriteGuard security;std::lock_guard<std::recursive_mutex> lock(mutex_);auto value=std::unique_ptr<StoredCalendarEvent>(new(std::nothrow) StoredCalendarEvent);if(!value){error="memory";return false;}
 if(!storageHealthy_||!CalendarEventStore::read(id,*value,error))return false;
 if(value->event.revision!=expected){error="revision_conflict";return false;}
 if(!canEditCalendar(value->event.calendarId)||(value->event.flags&(EventReadOnly|EventExternal|EventDeleted))){error="event_read_only";return false;}
 if(occurrence&&value->rules.repeat!=calendar::Repeat::None){
  const auto began=millis();bool found=false;
  if(!calendar::occurrences(value->event.startUtc,value->event.endUtc,value->rules,occurrence,occurrence+1,[&](int64_t s,int64_t){found|=s==occurrence;return !found;},[&](){NetworkWorker::instance().maintain();return uint32_t(millis()-began)<2000;})){error="recurrence_budget";return false;}
  if(!found||value->rules.excludedCount>=32){error="recurrence_exception_limit_or_invalid";return false;}value->rules.excluded[value->rules.excludedCount++]=occurrence;
 }else value->event.flags|=EventDeleted;
 if(++value->event.revision==0){error="revision_limit";return false;}value->seal();if(!CalendarEventStore::write(*value,error))return false;bumpRevision();return true;
}
bool CalendarService::query(const Query& q,std::vector<Occurrence>& out,bool& more,String& error){
 std::lock_guard<std::recursive_mutex> profileLock(profileMutex());
 refreshExternalCalendars();
 std::lock_guard<std::recursive_mutex> lock(mutex_);out.clear();more=false;
 if(!storageHealthy_){error="calendar_storage_integrity";return false;}
 if(q.fromUtc<=0||q.toUtc<=q.fromUtc||q.toUtc-q.fromUtc>366LL*86400||q.limit<1||q.limit>32||q.offset>256||q.preset>2||q.selectedCalendarCount>4){error="query_argument";return false;}
 auto visible=[&](uint32_t id){for(size_t i=0;i<calendarCount_;++i){const auto& c=calendars_[i];if(c.id!=id)continue;
  if(!c.enabled)return false;
  if(c.kind==uint8_t(CalendarKind::BirthdaysVirtual)||c.kind==uint8_t(CalendarKind::HolidaysVirtual))return true;
  if(q.calendarId&&c.id!=q.calendarId)return false;
  if(q.selectedCalendarCount){bool selected=false;for(unsigned n=0;n<q.selectedCalendarCount;++n)selected|=q.selectedCalendars[n]==c.id;if(!selected)return false;}
  if(q.preset==1&&c.kind!=uint8_t(CalendarKind::Family)&&c.kind!=uint8_t(CalendarKind::Household))return false;
  if(q.preset==2&&((c.kind!=uint8_t(CalendarKind::Personal)&&c.kind!=uint8_t(CalendarKind::External))||!q.personId||c.ownerPersonId!=q.personId))return false;return true;}return false;};
 const size_t keep=q.offset+q.limit+1;out.reserve(keep);
 const auto began=millis();auto proceed=[&](){NetworkWorker::instance().maintain();return !(q.cancelled&&q.cancelled->load())&&uint32_t(millis()-began)<10000;};
 auto add=[&](EventRecord e,int64_t original){if((e.flags&EventDeleted)||e.endUtc<=q.fromUtc||e.startUtc>=q.toUtc||!visible(e.calendarId))return;
  bool shared=false;for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==e.calendarId){e.ownerPersonId=calendars_[i].ownerPersonId;shared=calendars_[i].kind==uint8_t(CalendarKind::BirthdaysVirtual)||calendars_[i].kind==uint8_t(CalendarKind::HolidaysVirtual);}
  if(!shared&&q.personId&&!q.selectedCalendarCount&&e.ownerPersonId!=q.personId&&!(e.participantMask&participantMaskForPersonId(q.personId)))return;
  if(q.afterStartUtc&&(e.startUtc<q.afterStartUtc||(e.startUtc==q.afterStartUtc&&e.localId<=q.afterId)))return;
  for(const auto& prior:out)if(prior.event.localId==e.localId&&prior.event.startUtc==e.startUtc)return;
  Occurrence value{e,original};auto where=std::lower_bound(out.begin(),out.end(),value,ordered);out.insert(where,value);if(out.size()>keep)out.pop_back();};
 if(!CalendarEventStore::visit([&](const StoredCalendarEvent& value){if(!proceed()){error=q.cancelled&&q.cancelled->load()?"cancelled":"query_deadline";return false;}if(!visible(value.event.calendarId)||value.event.flags&EventDeleted)return true;
  if(!calendar::occurrences(value.event.startUtc,value.event.endUtc,value.rules,q.fromUtc,q.toUtc,[&](int64_t s,int64_t e){auto event=value.event;event.startUtc=s;event.endUtc=e;add(event,s);return true;},proceed)){error="recurrence_budget";return false;}return true;},error)||error.length()){out.clear();return false;}
 uint32_t birthdays=0,holidays=0;for(size_t i=0;i<calendarCount_;++i){if(calendars_[i].kind==uint8_t(CalendarKind::BirthdaysVirtual))birthdays=calendars_[i].id;if(calendars_[i].kind==uint8_t(CalendarKind::HolidaysVirtual))holidays=calendars_[i].id;}
 LocalPhonebook contacts;
 if(visible(birthdays)&&!PhonebookService::instance().loadLocal(contacts)){out.clear();error="contact_storage_unavailable";return false;}
 JsonDocument profileLinks;if(visible(birthdays)&&!calendar::loadPersonOptions(profileLinks)){out.clear();error="person_preferences_corrupt";return false;}
 const auto first=calendar::localDate(q.fromUtc),last=calendar::localDate(q.toUtc-1);const auto settings=region();
 for(int year=first.year;year<=last.year;++year){
  if(visible(birthdays))for(size_t i=0;i<personCount_;++i){const auto& p=persons_[i];if(!(p.flags&1))continue;if(!calendar::validDate(year,p.birthdayMonth,p.birthdayDay))continue;
   char personKey[9];snprintf(personKey,sizeof(personKey),"%08lx",static_cast<unsigned long>(p.id));const String contactId=profileLinks["contacts"][personKey]|"";bool linked=false;for(const auto& c:contacts.contacts)if((contactId.length()?c.id==contactId:c.email.equalsIgnoreCase(p.email))&&c.birthday.month==p.birthdayMonth&&c.birthday.day==p.birthdayDay){linked=true;break;}if(linked)continue;
   const auto date=calendar::Date{year,p.birthdayMonth,p.birthdayDay};EventRecord event{};event.calendarId=birthdays;event.localId=0xB000000000000000ULL|(uint64_t(p.id)<<16)|uint16_t(year);event.flags=EventAllDay|EventReadOnly;
   event.startUtc=calendar::localEpoch(date);event.endUtc=calendar::localEpoch(calendar::dateFromDay(calendar::dayNumber(year,date.month,date.day)+1));event.participantMask=uint16_t(1u<<i);
   if(p.birthdayYear&&year>=p.birthdayYear)snprintf(event.title,sizeof(event.title),"%s · %u. Geburtstag",p.displayName,unsigned(year-p.birthdayYear));else snprintf(event.title,sizeof(event.title),"Geburtstag: %s",p.displayName);add(event,event.startUtc);
  }
  if(visible(birthdays))for(const auto& c:contacts.contacts){
   if(!proceed()){out.clear();error="cancelled_or_query_deadline";return false;}
   EventRecord event{};if(calendar::contactBirthday(c,year,birthdays,event)){event.flags=EventAllDay|EventReadOnly;add(event,event.startUtc);}
  }
  if(visible(holidays))calendar::holidays(year,settings,[&](calendar::Date date,const char* title){EventRecord event{};event.calendarId=holidays;event.localId=0xA000000000000000ULL|uint64_t(calendar::dayNumber(date.year,date.month,date.day));event.flags=EventAllDay|EventReadOnly;
   event.startUtc=calendar::localEpoch(date);event.endUtc=calendar::localEpoch(calendar::dateFromDay(calendar::dayNumber(date.year,date.month,date.day)+1));strlcpy(event.title,title,sizeof(event.title));add(event,event.startUtc);});
 }
 // Provider snapshots stay separate/read-only. No cloud data is appended locally.
 // This 51-KiB snapshot must not require one contiguous internal-RAM block.
 // Use the existing checked PSRAM allocator; allocation failure is a job error,
 // not an exception/abort in the worker (IDF builds have exceptions disabled).
 std::unique_ptr<EventRecord[],decltype(&heap_caps_free)> remote(allocatePsramArray<EventRecord>(kCloudCalendarEvents),heap_caps_free);if(!remote){out.clear();error="memory";return false;}
 for(uint8_t slot=0;slot<5;++slot){const auto n=CalendarSyncService::instance().copyEvents(slot,remote.get(),kCloudCalendarEvents);for(size_t i=0;i<n;++i)add(remote[i],remote[i].startUtc);}
 more=out.size()>size_t(q.offset+q.limit);const auto skip=std::min<size_t>(q.offset,out.size());out.erase(out.begin(),out.begin()+skip);if(out.size()>q.limit)out.resize(q.limit);return true;
}
}
