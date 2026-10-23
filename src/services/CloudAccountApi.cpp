#include "CloudAccountService.h"
#include "CredentialStore.h"
#include "StorageService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include "CalDavConnector.h"
#include "PhonebookService.h"
#include "TimeService.h"
#include "ConfigService.h"
#include "OAuthProtocol.h"
#include "../core/RequestDeadline.h"
#include <esp_heap_caps.h>
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <mbedtls/platform_util.h>
#include <memory>
#include <cstring>
namespace hp {namespace {
struct Lock {SemaphoreHandle_t m;explicit Lock(SemaphoreHandle_t value,TickType_t wait=portMAX_DELAY):m(value){if(m&&xSemaphoreTakeRecursive(m,wait)!=pdTRUE)m=nullptr;}explicit operator bool()const{return m!=nullptr;}~Lock(){if(m)xSemaphoreGiveRecursive(m);}};
template<class T>struct PrivateRecord {T value{};~PrivateRecord(){mbedtls_platform_zeroize(&value,sizeof(value));}};
String metaPath(uint8_t slot){return String("/accounts/account_")+slot+".json";}
String davPath(uint8_t slot){return String("/accounts/dav_")+slot+".bin";}
String davKey(uint8_t slot){return String("dav_")+slot;}
bool publicText(const String& text,size_t maximum){if(!text.length()||text.length()>maximum)return false;for(size_t i=0;i<text.length();++i)if(uint8_t(text[i])<32||uint8_t(text[i])==127)return false;return true;}
}
int CalendarSyncService::accountSlot(const String& id){return id.length()==9&&id.startsWith("account-")&&id[8]>='0'&&id[8]<='4'?id[8]-'0':-1;}
AccountInfo CalendarSyncService::cloudInfo(uint8_t slot){
 AccountInfo result;result.id=String("account-")+slot;result.enabled=cloudEnabled_[slot];result.phonebookEnabled=phonebookEnabled_[slot];auto& state=info_[slot];
 result.email=state.email;result.displayName=state.name.length()?state.name:state.email;result.message=state.message;
 if(davConfigured_[slot]){result.provider=AccountProvider(dav_[slot].provider);result.displayName=dav_[slot].displayName;
 result.supportedCapabilities=capabilityBit(AccountCapability::Calendar)|capabilityBit(AccountCapability::Contacts);result.state=cloudStatus_[slot];}
 else {result.provider=state.provider==EmailProvider::Microsoft?AccountProvider::Microsoft:state.provider==EmailProvider::Apple?AccountProvider::ICloud:state.provider==EmailProvider::Other?AccountProvider::CalDav:AccountProvider::Google;
 if(state.provider==EmailProvider::Google||state.provider==EmailProvider::Microsoft)result.supportedCapabilities=capabilityBit(AccountCapability::Profile)|capabilityBit(AccountCapability::Calendar)|capabilityBit(AccountCapability::Contacts);
 else if(state.provider==EmailProvider::Apple||state.provider==EmailProvider::Other)result.supportedCapabilities=capabilityBit(AccountCapability::Calendar);
 switch(state.authState){case AccountAuthState::Idle:case AccountAuthState::Cancelled:result.state=state.connected?CloudAccountState::Connected:CloudAccountState::Disconnected;break;
 case AccountAuthState::RequestingCode:result.state=CloudAccountState::Connecting;break;
 case AccountAuthState::AwaitingUser:case AccountAuthState::Polling:result.state=CloudAccountState::AwaitingUser;break;
 case AccountAuthState::Connected:result.state=CloudAccountState::Connected;break;
 case AccountAuthState::Expired:result.state=CloudAccountState::TokenExpired;break;
 case AccountAuthState::Error:result.state=cloudStatus_[slot]==CloudAccountState::Disconnected||cloudStatus_[slot]==CloudAccountState::Connected?CloudAccountState::Error:cloudStatus_[slot];break;}
 }
 result.connected=state.connected&&result.enabled;
 if(result.connected){if(davConfigured_[slot]){if(state.davCalendarConnected)result.capabilities=capabilityBit(AccountCapability::Calendar);
 if(result.phonebookEnabled&&state.contactsConnected)result.capabilities|=capabilityBit(AccountCapability::Contacts);}
 else {const auto granted=AccountAuthPolicy::active(state.authState)?previousPurpose_[slot]:state.purpose;
 result.capabilities=capabilityBit(AccountCapability::Profile);if(AccountAuthPolicy::hasCalendar(granted))result.capabilities|=capabilityBit(AccountCapability::Calendar);
 if(result.phonebookEnabled&&AccountAuthPolicy::hasContacts(granted))result.capabilities|=capabilityBit(AccountCapability::Contacts);}}
 if(permissionMissing_[slot]){result.state=CloudAccountState::PermissionMissing;result.capabilities&=~capabilityBit(missingCapability_[slot]);}
 if(!result.enabled){result.state=CloudAccountState::Disconnected;result.capabilities=0;}
 return result;
}
std::vector<AccountInfo> CalendarSyncService::accounts(){Lock lock(mutex_);std::vector<AccountInfo> result;
 for(uint8_t slot=0;slot<5;++slot)if(loaded_[slot]&&(info_[slot].configured||davConfigured_[slot])&&(davConfigured_[slot]||info_[slot].provider==EmailProvider::Google||info_[slot].provider==EmailProvider::Microsoft||info_[slot].provider==EmailProvider::Apple||info_[slot].provider==EmailProvider::Other))result.push_back(cloudInfo(slot));return result;}
AccountInfo CalendarSyncService::accountInfo(const String& id){Lock lock(mutex_);const int slot=accountSlot(id);if(slot<0||!loaded_[slot])return AccountInfo{};return cloudInfo(slot);}
std::vector<AccountInfo> CalendarSyncService::accountsWithCapability(AccountCapability capability){auto result=accounts();const auto bit=capabilityBit(capability);
 for(auto i=result.begin();i!=result.end();)if(!bit||!(i->capabilities&bit))i=result.erase(i);else ++i;return result;}
bool CalendarSyncService::isConnected(const String& id){return accountInfo(id).connected;}
AccountCapabilities CalendarSyncService::capabilities(const String& id){return accountInfo(id).capabilities;}
CloudAccountState CalendarSyncService::requestCapability(const String& id,AccountCapability capability){Lock lock(mutex_);const int slot=accountSlot(id);
 if(slot<0||!loaded_[slot]||!info_[slot].configured||!capabilityBit(capability))return CloudAccountState::Error;
 const auto account=cloudInfo(slot);if(account.capabilities&capabilityBit(capability))return CloudAccountState::Connected;
 permissionMissing_[slot]=true;missingCapability_[slot]=capability;info_[slot].message=capability==AccountCapability::Calendar?"Kalenderberechtigung fehlt. Konten öffnen und Kalender freigeben.":capability==AccountCapability::Contacts?"Telefonbuch einschalten und Kontaktzugriff ausdrücklich freigeben.":"Diese Funktion besitzt noch keinen freigegebenen zentralen Connector.";
 return CloudAccountState::PermissionMissing;
}
bool CalendarSyncService::authorizeCapability(const String& id,AccountCapability capability){const int slot=accountSlot(id);if(slot<0)return false;
 EmailProvider provider;OAuthPurpose purpose;bool connected,phonebook;{Lock lock(mutex_);if(!loaded_[slot]||!cloudEnabled_[slot])return false;provider=info_[slot].provider;purpose=info_[slot].purpose;connected=info_[slot].connected;phonebook=phonebookEnabled_[slot];
 if(capability==AccountCapability::Contacts&&!phonebookEnabled_[slot])return false;}
 if(provider==EmailProvider::Apple||provider==EmailProvider::Other)return (capability==AccountCapability::Calendar||capability==AccountCapability::Contacts)&&testCalDav(slot);
 if(capability!=AccountCapability::Profile&&capability!=AccountCapability::Calendar&&capability!=AccountCapability::Contacts)return false;
 const bool calendar=capability==AccountCapability::Calendar||(connected&&AccountAuthPolicy::hasCalendar(purpose));
 const bool contacts=capability==AccountCapability::Contacts||(phonebook&&connected&&AccountAuthPolicy::hasContacts(purpose));
 return startLogin(provider,calendar?(contacts?OAuthPurpose::CalendarContacts:OAuthPurpose::Calendar):(contacts?OAuthPurpose::Contacts:OAuthPurpose::Identity),slot);
}
bool CalendarSyncService::disconnectAccount(const String& id){const int slot=accountSlot(id);return slot>=0&&disconnect(slot);}
bool CalendarSyncService::removeAccount(const String& id){SecurityWriteGuard security;const int slot=accountSlot(id);
 if(slot<0||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 {Lock lock(mutex_);forgetAccount_[slot]=true;}return disconnect(slot);
}
bool CalendarSyncService::persistCloudMetadata(uint8_t slot){
 AccountInfo account;{Lock lock(mutex_);account=cloudInfo(slot);}JsonDocument doc;doc["version"]=1;doc["accountId"]=account.id;
 doc["provider"]=uint8_t(account.provider);doc["displayName"]=account.displayName;doc["email"]=account.email;doc["capabilities"]=account.capabilities;doc["enabled"]=account.enabled;doc["phonebookEnabled"]=account.phonebookEnabled;
 String body;serializeJson(doc,body);return StorageService::instance().writeAtomic(StorageArea::Config,metaPath(slot),body.c_str(),body.length());
}
bool CalendarSyncService::setAccountEnabled(const String& id,bool enabled){SecurityWriteGuard security;const int slot=accountSlot(id);
 if(slot<0||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;Lock lock(mutex_);
 if(!loaded_[slot]||!info_[slot].configured||davConfigPending_[slot])return false;
 auto* job=new(std::nothrow) EnableJob{uint8_t(slot),generation_[slot],enabled};if(!job)return false;davConfigPending_[slot]=true;
 if(!NetworkWorker::instance().submit(runEnable,job,"account-enable")){delete job;davConfigPending_[slot]=false;return false;}return true;
}
void CalendarSyncService::runEnable(void* pointer){std::unique_ptr<EnableJob> job(static_cast<EnableJob*>(pointer));auto& service=instance();SecurityWriteGuard security;
 bool previous;{Lock lock(service.mutex_);service.davConfigPending_[job->slot]=false;
 if(service.generation_[job->slot]!=job->generation||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return;
 previous=job->phonebook?service.phonebookEnabled_[job->slot]:service.cloudEnabled_[job->slot];
 if(job->phonebook)service.phonebookEnabled_[job->slot]=job->enabled;else service.cloudEnabled_[job->slot]=job->enabled;}
 if(!service.persistCloudMetadata(job->slot)){Lock lock(service.mutex_);if(job->phonebook)service.phonebookEnabled_[job->slot]=previous;else service.cloudEnabled_[job->slot]=previous;return;}
 if(job->phonebook){Lock lock(service.mutex_);auto& info=service.info_[job->slot];info.contactsMessage=job->enabled?"Telefonbuch eingeschaltet. Kontaktfreigabe verbinden und synchronisieren.":"Telefonbuch-Synchronisierung ausgeschaltet.";
   if(!job->enabled){info.contactsConnected=false;if(AccountAuthPolicy::hasContacts(service.sessions_[job->slot].purpose)&&service.sessions_[job->slot].expires){++service.generation_[job->slot];service.clearSession(job->slot);info.purpose=service.previousPurpose_[job->slot];info.authState=info.connected?AccountAuthState::Connected:AccountAuthState::Idle;}}
   else service.nextSync_[job->slot]=0;return;}
 Lock lock(service.mutex_);if(!job->enabled){++service.generation_[job->slot];service.pending_[job->slot]=false;service.clearSession(job->slot);service.clearAccess(job->slot);service.info_[job->slot].working=false;}
 else service.restored_[job->slot]=false;
 service.calendarDataChanged();
}
bool CalendarSyncService::loadDav(uint8_t slot){
 char json[1537]{};size_t size=0;bool enabled=true,phonebook=false;String name,optInEmail;
 auto& storage=StorageService::instance();bool metadataExists=false;
 for(const char* suffix:{"",".bak",".tmp"})metadataExists|=storage.exists(StorageArea::Config,metaPath(slot)+suffix);
 enabled=!metadataExists;
 auto validateMeta=[slot](fs::FS& fs,const String& path){auto file=fs.open(path,"r");if(!file||file.size()>1536)return false;char buffer[1537]{};const size_t length=file.readBytes(buffer,file.size());JsonDocument doc;
 return !deserializeJson(doc,buffer,length)&&(doc["version"]|0)==1&&String(doc["accountId"]|"")==String("account-")+slot&&doc["enabled"].is<bool>();};
 if(metadataExists&&!storage.recoverAtomicWrite(StorageArea::Config,metaPath(slot),validateMeta))enabled=false;
 if(storage.read(StorageArea::Config,metaPath(slot),json,sizeof(json)-1,&size)){
 JsonDocument doc;if(!deserializeJson(doc,json,size)&&(doc["version"]|0)==1&&String(doc["accountId"]|"")==String("account-")+slot&&doc["enabled"].is<bool>()){enabled=doc["enabled"];name=doc["displayName"]|"";
 phonebook=doc["phonebookEnabled"].is<bool>()&&doc["phonebookEnabled"].as<bool>();optInEmail=doc["email"]|"";}}
 auto validateDav=[](fs::FS& fs,const String& path){auto file=fs.open(path,"r");DavMetadata record{};return file&&file.size()==sizeof(record)&&file.readBytes(reinterpret_cast<char*>(&record),sizeof(record))==sizeof(record)&&record.version==1&&
 (record.provider==uint8_t(AccountProvider::ICloud)||record.provider==uint8_t(AccountProvider::CalDav))&&memchr(record.server,0,sizeof(record.server))&&memchr(record.user,0,sizeof(record.user))&&memchr(record.displayName,0,sizeof(record.displayName))&&CalDavDiscovery::httpsUrl(record.server);};
 storage.recoverAtomicWrite(StorageArea::Config,davPath(slot),validateDav);
 DavMetadata record{};size=0;const bool valid=storage.read(StorageArea::Config,davPath(slot),&record,sizeof(record),&size)&&size==sizeof(record)&&record.version==1&&record.enabled<=1&&
 (record.provider==uint8_t(AccountProvider::ICloud)||record.provider==uint8_t(AccountProvider::CalDav))&&memchr(record.server,0,sizeof(record.server))&&memchr(record.user,0,sizeof(record.user))&&
 memchr(record.displayName,0,sizeof(record.displayName))&&CalDavDiscovery::httpsUrl(record.server);
 const auto account=EmailAccountService::instance().account(slot);phonebook=phonebook&&optInEmail.length()&&(valid?optInEmail==record.user:account.configured&&optInEmail==account.address);
 Lock lock(mutex_);cloudEnabled_[slot]=enabled;phonebookEnabled_[slot]=phonebook;info_[slot].name=name;davConfigured_[slot]=valid;if(valid)dav_[slot]=record;return valid;
}
bool CalendarSyncService::setPhonebookEnabled(const String& id,bool enabled){SecurityWriteGuard security;const int slot=accountSlot(id);
 if(slot<0||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;Lock lock(mutex_);
 if(!loaded_[slot]||!info_[slot].configured||davConfigPending_[slot]||deleting_[slot])return false;
 auto* job=new(std::nothrow) EnableJob{uint8_t(slot),generation_[slot],enabled,true};if(!job)return false;davConfigPending_[slot]=true;
 if(!NetworkWorker::instance().submit(runEnable,job,"phonebook-enable")){delete job;davConfigPending_[slot]=false;return false;}return true;
}
String CalendarSyncService::calDavServer(uint8_t slot){Lock lock(mutex_);return slot<5&&davConfigured_[slot]?String(dav_[slot].server):String();}
bool CalendarSyncService::configureCalDav(uint8_t slot,AccountProvider provider,const String& inputServer,const String& user,const char* password){
 SecurityWriteGuard security;if(slot>=5||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()||
 (provider!=AccountProvider::ICloud&&provider!=AccountProvider::CalDav)||!publicText(user,254))return false;
 const String server=provider==AccountProvider::ICloud?String("https://caldav.icloud.com/"):inputServer;
 if(server.length()>512||!CalDavDiscovery::httpsUrl(server.c_str()))return false;
 const auto existing=EmailAccountService::instance().account(slot);if(existing.configured&&existing.provider!=EmailProvider::Apple&&existing.provider!=EmailProvider::Other)return false;
 {Lock lock(mutex_);if(pending_[slot]||deleting_[slot])return false;}
 PrivateRecord<DavSecret> secret;const size_t length=password?strnlen(password,sizeof(secret.value.password)):0;if(length>=sizeof(secret.value.password))return false;
 if(length){if(provider==AccountProvider::ICloud&&!CalDavDiscovery::appleAppPassword(password))return false;
 for(size_t i=0;i<length;++i)if(uint8_t(password[i])<32||uint8_t(password[i])==127)return false;memcpy(secret.value.password,password,length);}
 else if(CredentialStore::instance().readOAuthRecord(davKey(slot).c_str(),&secret.value,sizeof(secret.value))&&secret.value.version==1&&
 memchr(secret.value.server,0,sizeof(secret.value.server))&&memchr(secret.value.user,0,sizeof(secret.value.user))&&memchr(secret.value.password,0,sizeof(secret.value.password))&&
 secret.value.provider==uint8_t(provider)&&server==secret.value.server&&user==secret.value.user){}
 else {mbedtls_platform_zeroize(&secret.value,sizeof(secret.value));EmailProvider emailProvider;char address[255]{};
 if(!existing.configured||!EmailAccountService::instance().loadCredentials(slot,address,sizeof(address),secret.value.password,sizeof(secret.value.password),emailProvider)||user!=address)return false;
 if(provider==AccountProvider::ICloud&&!CalDavDiscovery::appleAppPassword(secret.value.password))return false;}
 if(!secret.value.password[0])return false;secret.value.version=1;secret.value.provider=uint8_t(provider);strlcpy(secret.value.server,server.c_str(),sizeof(secret.value.server));strlcpy(secret.value.user,user.c_str(),sizeof(secret.value.user));
 DavMetadata metadata;metadata.provider=uint8_t(provider);strlcpy(metadata.server,server.c_str(),sizeof(metadata.server));strlcpy(metadata.user,user.c_str(),sizeof(metadata.user));strlcpy(metadata.displayName,user.c_str(),sizeof(metadata.displayName));
 if(!CredentialStore::instance().writeOAuthRecord(davKey(slot).c_str(),&secret.value,sizeof(secret.value)))return false;
 if(!StorageService::instance().writeAtomic(StorageArea::Config,davPath(slot),&metadata,sizeof(metadata))){invalidate(slot);Lock lock(mutex_);cloudStatus_[slot]=CloudAccountState::Error;info_[slot].message="Serverkonfiguration nicht gespeichert. Erneut einrichten.";return false;}
 {Lock lock(mutex_);++generation_[slot];clearSession(slot);clearAccess(slot);free(events_[slot]);events_[slot]=nullptr;free(details_[slot]);details_[slot]=nullptr;phonebookEnabled_[slot]=false;info_[slot].eventCount=0;
 dav_[slot]=metadata;davConfigured_[slot]=true;cloudEnabled_[slot]=true;loaded_[slot]=true;restored_[slot]=true;
 info_[slot].configured=true;info_[slot].provider=provider==AccountProvider::ICloud?EmailProvider::Apple:EmailProvider::Other;info_[slot].email=user;info_[slot].name=user;info_[slot].connected=false;
 cloudStatus_[slot]=CloudAccountState::Disconnected;info_[slot].message="Geschützt gespeichert. Verbindung jetzt testen.";permissionMissing_[slot]=false;collections_[slot].clear();}
 return persistCloudMetadata(slot);
}
bool CalendarSyncService::requestCalDavConfiguration(uint8_t slot,AccountProvider provider,const String& server,const String& user,const char* password){
 SecurityWriteGuard security;if(slot>=5||!publicText(user,254)||server.length()>512||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 if(provider!=AccountProvider::ICloud&&(provider!=AccountProvider::CalDav||!CalDavDiscovery::httpsUrl(server.c_str())))return false;
 if(!password||strnlen(password,193)>=193)return false;
 Lock lock(mutex_);if(davConfigPending_[slot])return false;auto* job=new(std::nothrow) DavConfigJob{slot,generation_[slot],provider,server,user};if(!job)return false;
 strlcpy(job->password,password,sizeof(job->password));
 davConfigPending_[slot]=true;if(!NetworkWorker::instance().submit(runDavConfig,job,"caldav-config")){davConfigPending_[slot]=false;delete job;return false;}return true;
}
void CalendarSyncService::runDavConfig(void* pointer){std::unique_ptr<DavConfigJob> job(static_cast<DavConfigJob*>(pointer));auto& service=instance();
 SecurityWriteGuard security;{Lock lock(service.mutex_);if(service.generation_[job->slot]!=job->generation){service.davConfigPending_[job->slot]=false;return;}}
 const bool ok=service.configureCalDav(job->slot,job->provider,job->server,job->user,job->password);
 {Lock lock(service.mutex_);service.davConfigPending_[job->slot]=false;if(!ok){service.cloudStatus_[job->slot]=CloudAccountState::NeedsLogin;service.info_[job->slot].message="Einrichtung fehlgeschlagen. PIN, Kontoplatz und App-Passwort pruefen; erneut ueber HTTPS oder am Display speichern.";}}
 if(ok)service.testCalDav(job->slot);
}
bool CalendarSyncService::testCalDav(uint8_t slot){SecurityWriteGuard security;if(slot>=5||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 Lock lock(mutex_);if(!davConfigured_[slot]||!cloudEnabled_[slot]||deleting_[slot])return false;
 if(!enqueue(slot,Operation::Sync))return false;cloudStatus_[slot]=CloudAccountState::Connecting;info_[slot].message="CalDAV-Verbindung wird im Hintergrund geprüft.";return true;
}
bool CalendarSyncService::executeDav(Job& job){
 struct Context{CalendarSyncService* service;Job* job;} context{this,&job};
 CalDavControl control;control.context=&context;
 control.current=[](void* value){auto& c=*static_cast<Context*>(value);return c.service->current(*c.job);};
 DavMetadata metadata;{Lock lock(mutex_);if(!davConfigured_[job.slot])return false;metadata=dav_[job.slot];if(!current(job))return true;
 loaded_[job.slot]=true;info_[job.slot].configured=true;info_[job.slot].provider=metadata.provider==uint8_t(AccountProvider::ICloud)?EmailProvider::Apple:EmailProvider::Other;
 info_[job.slot].email=metadata.user;info_[job.slot].name=metadata.displayName;info_[job.slot].purpose=OAuthPurpose::Calendar;
 if(!cloudEnabled_[job.slot])return true;}
 PrivateRecord<DavSecret> secret;const bool valid=CredentialStore::instance().readOAuthRecord(davKey(job.slot).c_str(),&secret.value,sizeof(secret.value))&&secret.value.version==1&&secret.value.provider==metadata.provider&&
 memchr(secret.value.server,0,sizeof(secret.value.server))&&memchr(secret.value.user,0,sizeof(secret.value.user))&&memchr(secret.value.password,0,sizeof(secret.value.password))&&
 String(secret.value.server)==metadata.server&&String(secret.value.user)==metadata.user&&secret.value.password[0];
 if(job.operation==Operation::Restore){if(valid)restoreCalendarCache(job);Lock lock(mutex_);if(current(job)){cloudStatus_[job.slot]=valid?CloudAccountState::ServerUnavailable:CloudAccountState::NeedsLogin;info_[job.slot].message=valid?"Lokaler CalDAV-Cache geladen; Verbindung wird im Hintergrund geprüft.":"CalDAV-App-Passwort fehlt.";nextSync_[job.slot]=0;}return true;}
 CalDavConnectionResult result;if(valid)result=CalDavConnector::discover(metadata.server,metadata.user,secret.value.password,metadata.provider==uint8_t(AccountProvider::ICloud),&control);
 else {result.state=CloudAccountState::NeedsLogin;result.message="Geschütztes App-Passwort fehlt oder passt nicht zur Serverkonfiguration.";}
 if(result.state==CloudAccountState::Connected){
  auto* events=static_cast<EventRecord*>(heap_caps_calloc(kCloudCalendarEvents,sizeof(EventRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  auto* details=static_cast<EventDetailsSnapshot*>(heap_caps_malloc(sizeof(EventDetailsSnapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(details)new(details) EventDetailsSnapshot{};uint8_t count=0;bool complete=events&&details;
  String failure=complete?String():String("Nicht genügend CalDAV-Cache-Speicher.");
  const int64_t now=TimeService::instance().nowUtc(),from=job.rangeFrom?job.rangeFrom:now-86400,to=job.rangeTo?job.rangeTo:now+30*86400;
  const auto selected=ConfigService::instance().getString((String("cal_sel_")+job.slot).c_str(),"");JsonDocument selection;
  bool explicitSelection=false;
  if(selected.length()){
   if(selected.length()>4096||deserializeJson(selection,selected)||!selection.is<JsonObject>()||!selection["items"].is<JsonArray>()||selection["items"].size()>16){complete=false;failure="Ungültige Kalenderauswahl; letzten Cache behalten.";}
   else explicitSelection=String(selection["server"]|"")==metadata.server&&String(selection["user"]|"")==metadata.user;
  }
  for(const auto& collection:result.calendars){if(!complete||!current(job))break;bool enabled=!explicitSelection;for(const auto& id:selection["items"].as<JsonArrayConst>())if(String(id.as<const char*>())==collection.id)enabled=true;if(!enabled)continue;
   auto data=CalDavConnector::events(metadata.server,collection.id.c_str(),metadata.user,secret.value.password,metadata.provider==uint8_t(AccountProvider::ICloud),from,to,&control);
   if(data.state!=CloudAccountState::Connected){complete=false;failure=data.message;break;}
   if(data.events.size()>kCloudCalendarEvents-count){complete=false;failure="CalDAV-Zeitraum überschreitet 128 Termine; Zeitraum verkleinern.";break;}
   for(const auto& imported:data.events){
    uint64_t hash=14695981039346656037ULL;const String identity=String(job.slot)+"|"+collection.id+"|"+imported.uid.c_str()+"|"+String(static_cast<long long>(imported.start));for(unsigned char byte:identity){hash^=byte;hash*=1099511628211ULL;}
    const uint64_t id=0xC000000000000000ULL|(hash&0x0fffffffffffffffULL);bool collision=false;for(unsigned i=0;i<count;++i)if(events[i].localId==id)collision=true;
    if(collision){complete=false;failure="CalDAV-Terminidentität kollidiert; letzten Cache behalten.";break;}
    auto& event=events[count];event.localId=id;event.calendarId=0x80000000u+job.slot;event.revision=1;event.flags=EventReadOnly|EventExternal|(imported.allDay?EventAllDay:0);event.startUtc=imported.start;event.endUtc=imported.end;
    detailPreview(event.uid,sizeof(event.uid),imported.uid.c_str());detailPreview(event.title,sizeof(event.title),imported.title.empty()?"Ohne Titel":imported.title.c_str());detailPreview(event.location,sizeof(event.location),imported.location.c_str());detailPreview(event.notes,sizeof(event.notes),imported.notes.c_str());
    auto& detail=details->items[count++];detail.localId=id;detailPreview(detail.location,sizeof(detail.location),imported.location.c_str());detailPreview(detail.notes,sizeof(detail.notes),imported.notes.c_str());detail.truncated=imported.location.size()>=sizeof(detail.location)||imported.notes.size()>=sizeof(detail.notes);
   }
  }
  if(complete&&current(job)){if(!commitCalendarCache(job,events,details,count,false)){result.message="CalDAV-Cache konnte nicht gespeichert werden.";}else result.message="CalDAV-Termine synchronisiert.";events=nullptr;details=nullptr;}
  else result.message=failure.length()?failure:String("CalDAV-Synchronisierung abgebrochen.");free(events);free(details);
 }
 CardDavConnectionResult contacts;bool phonebook=false;{Lock lock(mutex_);phonebook=current(job)&&phonebookEnabled_[job.slot]&&cloudEnabled_[job.slot];}
 if(valid&&phonebook)contacts=CalDavConnector::contacts(metadata.server,metadata.user,secret.value.password,metadata.provider==uint8_t(AccountProvider::ICloud),&control);
 if(phonebook){SecurityWriteGuard security;Lock lock(mutex_);if(current(job)&&phonebookEnabled_[job.slot]&&cloudEnabled_[job.slot]){
   auto& info=info_[job.slot];info.contactsConnected=false;info.contactsMessage=contacts.message;
   if(contacts.state==CloudAccountState::Connected&&PhonebookService::instance().saveFile(String("account-")+job.slot,true,contacts.contacts,contacts.truncated)){
       info.contactsConnected=true;info.contactCount=contacts.contacts.size();info.contactsTruncated=contacts.truncated;
   }else if(contacts.state==CloudAccountState::Connected)info.contactsMessage="CardDAV-Kontakte konnten nicht gespeichert werden.";
 }}
 {Lock lock(mutex_);if(!current(job))return true;info_[job.slot].davCalendarConnected=result.state==CloudAccountState::Connected;
 cloudStatus_[job.slot]=info_[job.slot].contactsConnected?CloudAccountState::Connected:result.state;
 info_[job.slot].connected=result.state==CloudAccountState::Connected||info_[job.slot].contactsConnected;info_[job.slot].authState=info_[job.slot].connected?AccountAuthState::Connected:AccountAuthState::Error;
 info_[job.slot].message=result.message;info_[job.slot].truncated=result.truncated;collections_[job.slot]=std::move(result.calendars);
 permissionMissing_[job.slot]=result.state==CloudAccountState::PermissionMissing;missingCapability_[job.slot]=AccountCapability::Calendar;nextSync_[job.slot]=millis()+(result.state==CloudAccountState::ServerUnavailable?15000:300000);}
 persistCloudMetadata(job.slot);return true;
}
std::vector<CloudCalendarCollection> CalendarSyncService::calendarCollections(const String& id){Lock lock(mutex_);const int slot=accountSlot(id);
 if(slot<0||!loaded_[slot]||!info_[slot].configured||(!davConfigured_[slot]&&!AccountAuthPolicy::hasCalendar(info_[slot].purpose)))return {};
   if(davConfigured_[slot]||!collections_[slot].empty())return collections_[slot];return {{"primary","Standardkalender",true}};
  }
std::vector<CloudCalendarCollection> CalendarSyncService::selectedOAuthCalendars(Job& job,EmailProvider provider,const String& email){
 auto all=calendarCollections(String("account-")+job.slot);const auto saved=ConfigService::instance().getString((String("cal_sel_")+job.slot).c_str(),"");
 if(!saved.length())return all;JsonDocument selection;
 if(saved.length()>4096||deserializeJson(selection,saved)||!selection.is<JsonObject>()||!selection["items"].is<JsonArray>()||selection["items"].size()>16)return {{"","Ungültige Kalenderauswahl",true}};
 const String owner=String("oauth:")+unsigned(provider);
 if(String(selection["server"]|"")!=owner||String(selection["user"]|"")!=email)return all;
 std::vector<CloudCalendarCollection> selected;
 for(const auto& collection:all)for(auto id:selection["items"].as<JsonArrayConst>())if(String(id.as<const char*>())==collection.id){selected.push_back(collection);break;}
 if(selected.size()!=selection["items"].size())return {{"","Kalenderliste erneut verbinden oder Auswahl prüfen",true}};
 return selected;
}
std::vector<CloudCalendarCollection> CloudCalendarClient::collections()const{return CloudAccountService::instance().calendarCollections(id_);}
bool CloudCalendarClient::copyEventDetails(uint64_t id,EventDetails& output)const{auto& service=CloudAccountService::instance();const int slot=service.accountSlot(id_);
 if(slot<0){output={};return false;}return service.copyEventDetails(uint8_t(slot),id,output);}
bool CloudCalendarClient::requestRefresh()const{auto& service=CloudAccountService::instance();const int slot=service.accountSlot(id_);
 return slot>=0&&(service.capabilities(id_)&capabilityBit(AccountCapability::Calendar))&&service.sync(slot);}
size_t CloudCalendarClient::copyEvents(EventRecord* output,size_t capacity)const{auto& service=CloudAccountService::instance();const int slot=service.accountSlot(id_);
 return slot>=0&&(service.capabilities(id_)&capabilityBit(AccountCapability::Calendar))?service.copyEvents(slot,output,capacity):0;}
void CalendarSyncService::publishAccountEvents(){
 for(uint8_t slot=0;slot<5;++slot){AccountInfo account;bool connected=false,disconnected=false,expired=false,error=false,permission=false,caps=false;
 {Lock lock(mutex_,0);if(!lock)return;if(!loaded_[slot])continue;account=cloudInfo(slot);connected=account.connected&&!lastPublishedConnected_[slot];disconnected=!account.connected&&lastPublishedConnected_[slot];
 caps=account.capabilities!=lastPublishedCaps_[slot];if(account.state!=lastPublishedState_[slot]){expired=account.state==CloudAccountState::NeedsLogin||account.state==CloudAccountState::TokenExpired;
 error=account.state==CloudAccountState::Error||account.state==CloudAccountState::ServerUnavailable||account.state==CloudAccountState::TlsError||account.state==CloudAccountState::InvalidCredentials;permission=account.state==CloudAccountState::PermissionMissing;}
 lastPublishedState_[slot]=account.state;lastPublishedConnected_[slot]=account.connected;lastPublishedCaps_[slot]=account.capabilities;}
 // Synchronous bus payload is valid only during handlers; copy it if retained.
 for(auto event:{EventType::AccountConnected,EventType::AccountDisconnected,EventType::AccountAuthExpired,EventType::AccountSyncError,EventType::AccountCapabilitiesChanged,EventType::AccountPermissionMissing}){
 bool send=event==EventType::AccountConnected?connected:event==EventType::AccountDisconnected?disconnected:event==EventType::AccountAuthExpired?expired:event==EventType::AccountSyncError?error:event==EventType::AccountCapabilitiesChanged?caps:permission;
 if(send)EventBus::instance().publish({event,&account});}
 }
}
}
