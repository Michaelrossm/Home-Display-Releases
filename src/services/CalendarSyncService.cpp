#include "CalendarSyncService.h"
#include "CredentialStore.h"
#include "NetworkWorker.h"
#include "NetworkService.h"
#include "SecurityService.h"
#include "StorageService.h"
#include "TimeService.h"
#include "OAuthProtocol.h"
#include "ConfigService.h"
#include "OAuthClientDefaults.h"
#include "PhonebookService.h"
#include "../core/RequestDeadline.h"
#include "../apps/lua/LuaEvents.h"
#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>
#include <memory>
#include <new>
#include <cstring>
#include <cstddef>
namespace hp {
namespace {
constexpr const char* googleScope = "openid email https://www.googleapis.com/auth/calendar.events.readonly https://www.googleapis.com/auth/calendar.calendarlist.readonly";
constexpr const char* googleIdentityScope = "openid email profile";
constexpr const char* msIdentityScope = "openid profile offline_access User.Read";
constexpr const char* msScope = "openid profile offline_access User.Read Calendars.Read";
String requestedScope(EmailProvider provider,OAuthPurpose purpose){
    String scope=provider==EmailProvider::Google?String(AccountAuthPolicy::hasCalendar(purpose)?googleScope:googleIdentityScope):String(AccountAuthPolicy::hasCalendar(purpose)?msScope:msIdentityScope);
    if(AccountAuthPolicy::hasContacts(purpose))scope+=provider==EmailProvider::Google?" https://www.googleapis.com/auth/contacts.readonly":" Contacts.Read";return scope;
}
constexpr const char* msToken = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
void wipe(String& text) { if(text.length()) mbedtls_platform_zeroize(&text[0],text.length()); text=""; }
template<class T> struct Erased {
    T value{};
    ~Erased(){mbedtls_platform_zeroize(&value,sizeof(value));}
};
template<class T> class HeapErased {
    T* record_;
public:
    HeapErased():record_(static_cast<T*>(heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))){
        if(record_)new(record_) T{};
    }
    ~HeapErased(){if(record_){mbedtls_platform_zeroize(record_,sizeof(T));free(record_);}}
    HeapErased(const HeapErased&)=delete;HeapErased& operator=(const HeapErased&)=delete;
    explicit operator bool()const{return record_!=nullptr;}
    T* operator->(){return record_;} T& operator*(){return *record_;}
};
// ArduinoJson pools contain bearer/refresh tokens, so erase before releasing.
class SecureAllocator : public ArduinoJson::Allocator {
    struct alignas(std::max_align_t) Header {size_t size;};
public:
    void* allocate(size_t size) override {
        auto* p=static_cast<Header*>(heap_caps_malloc(size+sizeof(Header),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!p)p=static_cast<Header*>(malloc(size+sizeof(Header)));
        if(!p)return nullptr; p->size=size; return p+1;
    }
    void deallocate(void* pointer) override {
        if(!pointer)return;auto* p=static_cast<Header*>(pointer)-1;
        const size_t size=p->size;mbedtls_platform_zeroize(p,size+sizeof(Header));free(p);
    }
    void* reallocate(void* pointer,size_t size) override {
        if(!pointer)return allocate(size);void* next=allocate(size);
        if(next){memcpy(next,pointer,std::min(size,(static_cast<Header*>(pointer)-1)->size));deallocate(pointer);}return next;
    }
};
struct Reply {
    String body; int status=0; bool overflow=false;
    ~Reply(){wipe(body);}
};
esp_err_t received(esp_http_client_event_t* event){
    auto* reply=static_cast<Reply*>(event->user_data);
    if(event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0){
        if(reply->body.length()+event->data_len>65536){reply->overflow=true;return ESP_FAIL;}
        if(!reply->body.concat(static_cast<const char*>(event->data),event->data_len)){reply->overflow=true;return ESP_FAIL;}
    }return ESP_OK;
}
bool request(const char* url, const String& body, const String& bearer, Reply& reply,uint32_t timeoutMs=10000){
    if(!NetworkService::instance().online()||!TimeService::instance().valid())return false;
    esp_http_client_config_t cfg{};cfg.url=url;cfg.crt_bundle_attach=esp_crt_bundle_attach;
    if(!timeoutMs)return false;
    cfg.timeout_ms=std::min<uint32_t>(10000,timeoutMs);cfg.disable_auto_redirect=true;cfg.event_handler=received;cfg.user_data=&reply;
    auto client=esp_http_client_init(&cfg);if(!client)return false;
    String auth;
    if(bearer.length()){auth="Bearer "+bearer;esp_http_client_set_header(client,"Authorization",auth.c_str());}
    esp_http_client_set_header(client,"Prefer","outlook.timezone=\"UTC\"");
    if(body.length()){
        esp_http_client_set_method(client,HTTP_METHOD_POST);
        esp_http_client_set_header(client,"Content-Type","application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client,body.c_str(),body.length());
    }
    const bool ok=esp_http_client_perform(client)==ESP_OK&&!reply.overflow;
    reply.status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);wipe(auth);return ok;
}
String form(const char* key,const String& value){return String(key)+"="+OAuthProtocol::encode(value);}
const char* providerKey(EmailProvider p,OAuthPurpose purpose){return p==EmailProvider::Microsoft?"oauth_ms":purpose==OAuthPurpose::Identity?"oauth_gdev":"oauth_google";}
const char* ownKey(EmailProvider p,OAuthPurpose purpose){return p==EmailProvider::Microsoft?"oa_ms_own":purpose==OAuthPurpose::Identity?"oa_gd_own":"oa_gw_own";}
const char* standardId(EmailProvider p,OAuthPurpose purpose){return p==EmailProvider::Microsoft?HOMEPANEL_MICROSOFT_CLIENT_ID:purpose==OAuthPurpose::Identity?HOMEPANEL_GOOGLE_DEVICE_CLIENT_ID:HOMEPANEL_GOOGLE_WEB_CLIENT_ID;}
bool verificationUrl(EmailProvider p,const String& link){
    if(link.indexOf('@')>=0||link.indexOf('\\')>=0||link.indexOf('#')>=0||link.length()>254)return false;
    for(size_t i=0;i<link.length();++i)if(uint8_t(link[i])<=32||uint8_t(link[i])>=127)return false;
    if(p==EmailProvider::Microsoft)return link=="https://microsoft.com/devicelogin"||link=="https://www.microsoft.com/devicelogin";
    return link=="https://www.google.com/device"||link.startsWith("https://www.google.com/device?")||
        link=="https://accounts.google.com/o/oauth2/device/usercode"||link.startsWith("https://accounts.google.com/o/oauth2/device/usercode?");
}
String tokenKey(uint8_t slot){return String("oauth_")+slot;}
String snapshotPath(uint8_t slot){return String("/calendar/remote_")+slot+".bin";}
String detailSnapshotPath(uint8_t slot){return String("/calendar/remote_details_")+slot+".bin";}
bool supported(EmailProvider p){return p==EmailProvider::Google||p==EmailProvider::Microsoft;}
bool due(uint32_t at){return int32_t(millis()-at)>=0;}
bool identity(const String& expected,const String& actual){
    String a=expected,b=actual;a.toLowerCase();b.toLowerCase();return a.length()&&a==b;
}
bool opaqueToken(const String& value,size_t maximum){
    if(!value.length()||value.length()>maximum)return false;
    for(size_t i=0;i<value.length();++i)if(uint8_t(value[i])<=32||uint8_t(value[i])>=127)return false;return true;
}
void utf8Copy(char* target,size_t capacity,const char* source){
    size_t count=std::min(strlen(source),capacity-1);
    while(count&&(uint8_t(source[count])&0xc0)==0x80)--count;
    memcpy(target,source,count);target[count]=0;
}
struct Guard {
    SemaphoreHandle_t mutex;
    explicit Guard(SemaphoreHandle_t m,TickType_t wait=portMAX_DELAY):mutex(m){if(m&&xSemaphoreTakeRecursive(m,wait)!=pdTRUE)mutex=nullptr;}
    explicit operator bool()const{return mutex!=nullptr;}
    ~Guard(){if(mutex)xSemaphoreGiveRecursive(mutex);}
};
}
CalendarSyncService& CalendarSyncService::instance(){static CalendarSyncService service;return service;}
bool CalendarSyncService::begin(){mutex_=xSemaphoreCreateRecursiveMutex();return mutex_!=nullptr;}
CalendarConnectionInfo CalendarSyncService::info(uint8_t slot){
    Guard lock(mutex_);if(slot>=5)return CalendarConnectionInfo{};
    auto result=info_[slot];result.slot=slot;
    result.expiresIn=AccountAuthPolicy::remaining(sessions_[slot].expires,millis());return result;
}
CalendarOAuthSetup& CalendarSyncService::setupRef(EmailProvider p,OAuthPurpose purpose){
    return p==EmailProvider::Microsoft?microsoft_:purpose==OAuthPurpose::Identity?googleDevice_:google_;
}
CalendarOAuthSetup CalendarSyncService::setup(EmailProvider p,OAuthPurpose purpose){Guard lock(mutex_);return setupRef(p,purpose);}
bool CalendarSyncService::providerRecord(EmailProvider p,ProviderRecord& r,OAuthPurpose purpose){
    if(!supported(p))return false;Erased<ProviderRecord> saved;
    const bool stored=CredentialStore::instance().readOAuthRecord(providerKey(p,purpose),&saved.value,sizeof(saved.value))&&saved.value.version==1&&
        memchr(saved.value.clientId,0,sizeof(saved.value.clientId))&&memchr(saved.value.redirect,0,sizeof(saved.value.redirect))&&memchr(saved.value.secret,0,sizeof(saved.value.secret));
    const bool own=ConfigService::instance().getBool(ownKey(p,purpose),stored);
    const String id=AccountAuthPolicy::resolveClientId(standardId(p,purpose),stored?String(saved.value.clientId):String(),own);
    strlcpy(r.clientId,id.c_str(),sizeof(r.clientId));
    if(p==EmailProvider::Google&&purpose!=OAuthPurpose::Identity)
        strlcpy(r.redirect,own&&stored?saved.value.redirect:HOMEPANEL_GOOGLE_REDIRECT_URI,sizeof(r.redirect));
    if(p==EmailProvider::Google&&stored&&id==saved.value.clientId)memcpy(r.secret,saved.value.secret,sizeof(r.secret));
    return AccountAuthPolicy::validClientId(id);
}
bool CalendarSyncService::tokenRecord(uint8_t slot,TokenRecord& r){
    return CredentialStore::instance().readOAuthRecord(tokenKey(slot).c_str(),&r,sizeof(r))&&(r.version==1||r.version==2)&&
        (r.provider&15)<=1&&(r.provider>>4)<=3&&(r.version!=1||r.provider<=1)&&memchr(r.email,0,sizeof(r.email))&&memchr(r.clientId,0,sizeof(r.clientId))&&
        memchr(r.refresh,0,sizeof(r.refresh))&&r.refresh[0];
}
void CalendarSyncService::loadSetups(){
    for(uint8_t index=0;index<3;++index){
        const auto p=index==0?EmailProvider::Microsoft:EmailProvider::Google;
        const auto purpose=index==1?OAuthPurpose::Identity:OAuthPurpose::Calendar;
        Erased<ProviderRecord> record;
        const bool stored=CredentialStore::instance().readOAuthRecord(providerKey(p,purpose),&record.value,sizeof(record.value));
        CalendarOAuthSetup data;data.own=ConfigService::instance().getBool(ownKey(p,purpose),stored);
        Erased<ProviderRecord> effective;providerRecord(p,effective.value,purpose);
        data.clientId=effective.value.clientId;data.redirect=effective.value.redirect;data.secretStored=effective.value.secret[0]!=0;
        data.standardAvailable=AccountAuthPolicy::validClientId(standardId(p,purpose));
        Guard lock(mutex_);auto& target=setupRef(p,purpose);
        data.busy=target.busy;data.message=target.message;target=data;
    }
}
void CalendarSyncService::clearSession(uint8_t slot){
    wipe(sessions_[slot].state);wipe(sessions_[slot].verifier);wipe(sessions_[slot].deviceCode);
    sessions_[slot]=Session{};info_[slot].authorizationUrl="";info_[slot].userCode="";
}
void CalendarSyncService::clearAccess(uint8_t slot){
    if(access_[slot]){mbedtls_platform_zeroize(access_[slot],strlen(access_[slot])+1);free(access_[slot]);access_[slot]=nullptr;}
    accessUntil_[slot]=0;
}
bool CalendarSyncService::configure(EmailProvider p,const String& id,const String& redirect,const char* secret,OAuthPurpose purpose){
    return configureRecord(p,id,redirect,secret,purpose,true,true);
}
bool CalendarSyncService::configureRecord(EmailProvider p,const String& id,const String& redirect,const char* secret,OAuthPurpose purpose,bool own,bool requireSecret){
    SecurityWriteGuard security;
    if(!supported(p)||purpose>OAuthPurpose::CalendarContacts||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    if(own){
        if(!AccountAuthPolicy::validClientId(id)||redirect.length()>254)return false;
        if(p==EmailProvider::Google&&purpose!=OAuthPurpose::Identity&&!OAuthProtocol::httpsRedirect(redirect))return false;
        Erased<ProviderRecord> record;
        const size_t size=secret?strnlen(secret,sizeof(record.value.secret)):0;
        if(size>=sizeof(record.value.secret))return false;
        if(p==EmailProvider::Google){
            if(!size){
                Erased<ProviderRecord> previous;
                if(CredentialStore::instance().readOAuthRecord(providerKey(p,purpose),&previous.value,sizeof(previous.value))&&
                    memchr(previous.value.clientId,0,sizeof(previous.value.clientId))&&id==previous.value.clientId&&
                    memchr(previous.value.secret,0,sizeof(previous.value.secret)))memcpy(record.value.secret,previous.value.secret,sizeof(record.value.secret));
            }else{
                for(size_t i=0;i<size;++i)if(uint8_t(secret[i])<=32||uint8_t(secret[i])>=127)return false;
                memcpy(record.value.secret,secret,size);
            }
            if(requireSecret&&!record.value.secret[0])return false;
        }else if(size)return false; // Public Microsoft client never accepts a secret.
        strlcpy(record.value.clientId,id.c_str(),sizeof(record.value.clientId));
        if(p==EmailProvider::Google&&purpose!=OAuthPurpose::Identity)strlcpy(record.value.redirect,redirect.c_str(),sizeof(record.value.redirect));
        if(!CredentialStore::instance().writeOAuthRecord(providerKey(p,purpose),&record.value,sizeof(record.value)))return false;
    }
    if(!ConfigService::instance().putBool(ownKey(p,purpose),own))return false;
    {Guard lock(mutex_);
        for(uint8_t slot=0;slot<5;++slot)if(loaded_[slot]&&info_[slot].provider==p&&(p==EmailProvider::Microsoft||info_[slot].purpose==purpose)){
            ++generation_[slot];pending_[slot]=false;clearSession(slot);clearAccess(slot);
            info_[slot].connected=false;info_[slot].authState=AccountAuthState::Idle;restored_[slot]=false;
            free(events_[slot]);events_[slot]=nullptr;free(details_[slot]);details_[slot]=nullptr;info_[slot].eventCount=0;
        }
    }loadSetups();return true;
}
bool CalendarSyncService::configurePublic(EmailProvider p,OAuthPurpose purpose,bool own,const String& id,const String& redirect){
    SecurityWriteGuard security;
    if(!supported(p)||purpose>OAuthPurpose::CalendarContacts||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()||
        (own&&!AccountAuthPolicy::validClientId(id))||(p==EmailProvider::Google&&purpose!=OAuthPurpose::Identity&&own&&!OAuthProtocol::httpsRedirect(redirect)))return false;
    Guard lock(mutex_);auto& data=setupRef(p,purpose);if(data.busy)return false;
    auto* job=new(std::nothrow) ConfigJob{p,purpose,own,id,redirect};if(!job)return false;
    data.busy=true;data.message="OAuth-Einstellungen werden gespeichert.";
    if(!NetworkWorker::instance().submit(runConfig,job,"oauth-config")){data.busy=false;delete job;return false;}return true;
}
void CalendarSyncService::runConfig(void* pointer){
    std::unique_ptr<ConfigJob> job(static_cast<ConfigJob*>(pointer));auto& service=instance();
    const bool ok=service.configureRecord(job->provider,job->clientId,job->redirect,"",job->purpose,job->own,false);
    Guard lock(service.mutex_);auto& data=service.setupRef(job->provider,job->purpose);data.busy=false;
    data.message=ok?"OAuth-Einstellungen gespeichert.":"Nicht gespeichert. Einstellungen entsperren und Angaben prüfen.";
}
uint8_t CalendarSyncService::selectSlot(EmailProvider p,bool freeAllowed){
    // Caller owns the account mutex; no NVS/filesystem access on HTTP/UI.
    for(uint8_t slot=0;slot<5;++slot)if(loaded_[slot]&&info_[slot].provider==p&&info_[slot].connected&&!deleting_[slot])return slot;
    for(uint8_t slot=0;slot<5;++slot)if(loaded_[slot]&&info_[slot].provider==p&&info_[slot].configured&&!deleting_[slot])return slot;
    if(freeAllowed)for(uint8_t slot=0;slot<5;++slot)if(loaded_[slot]&&!info_[slot].configured&&!pending_[slot]&&!deleting_[slot]&&!AccountAuthPolicy::active(info_[slot].authState))return slot;
    return 255;
}
CalendarConnectionInfo CalendarSyncService::getAccountState(EmailProvider p){
    if(!supported(p))return CalendarConnectionInfo{};Guard lock(mutex_);
    uint8_t slot=lastLoginSlot_[uint8_t(p)];
    if(slot>=5||info_[slot].provider!=p)slot=selectSlot(p,false);
    if(slot<5)return info(slot);
    auto data=providerStatus_[uint8_t(p)];data.provider=p;return data;
}
bool CalendarSyncService::startMicrosoftLogin(){return startLogin(EmailProvider::Microsoft,OAuthPurpose::Identity);}
bool CalendarSyncService::startGoogleLogin(OAuthPurpose purpose){return startLogin(EmailProvider::Google,purpose);}
bool CalendarSyncService::startLogin(EmailProvider p,OAuthPurpose purpose,uint8_t target){
    SecurityWriteGuard security;
    if(!supported(p)||purpose>OAuthPurpose::CalendarContacts||!mutex_||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    Guard lock(mutex_);auto& failure=providerStatus_[uint8_t(p)];failure.provider=p;failure.authState=AccountAuthState::Error;
    for(uint8_t existing=0;existing<5;++existing)
        if(info_[existing].provider==p&&AccountAuthPolicy::active(info_[existing].authState))return false;
    const uint8_t slot=target<5?target:selectSlot(p,true);
    if(slot>=5){lastLoginSlot_[uint8_t(p)]=255;failure.message="Kein freier Kontoplatz oder Konten werden noch geladen.";return false;}
    if(pending_[slot]||deleting_[slot]||(AccountAuthPolicy::hasContacts(purpose)&&!phonebookEnabled_[slot]))return false;
    if(!loaded_[slot]||davConfigured_[slot]||!cloudEnabled_[slot]||(info_[slot].configured&&info_[slot].provider!=p))return false;
    permissionMissing_[slot]=false;
    ++generation_[slot];clearSession(slot);
    previousPurpose_[slot]=info_[slot].purpose;
    info_[slot].provider=p;info_[slot].purpose=purpose;identify_[slot]=true;lastLoginSlot_[uint8_t(p)]=slot;
    if(!enqueue(slot,Operation::Connect)){info_[slot].authState=AccountAuthState::Error;info_[slot].message="Dienst beschäftigt. Erneut versuchen.";return false;}
    info_[slot].authState=AccountAuthState::RequestingCode;info_[slot].message="Anmeldung wird vorbereitet.";return true;
}
bool CalendarSyncService::cancelLogin(EmailProvider p){
    SecurityWriteGuard security;
    if(!supported(p)||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    Guard lock(mutex_);const auto slot=lastLoginSlot_[uint8_t(p)];if(slot>=5||info_[slot].provider!=p||!AccountAuthPolicy::active(info_[slot].authState))return false;
    ++generation_[slot];pending_[slot]=false;clearSession(slot);restored_[slot]=true;
    if(info_[slot].connected)info_[slot].purpose=previousPurpose_[slot];
    info_[slot].working=false;info_[slot].authState=AccountAuthState::Cancelled;info_[slot].message="Anmeldung abgebrochen.";return true;
}
bool CalendarSyncService::disconnectAccount(EmailProvider p){
    SecurityWriteGuard security;
    if(!supported(p)||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    bool selected[5]{};{Guard lock(mutex_);for(uint8_t slot=0;slot<5;++slot)
        selected[slot]=info_[slot].provider==p&&(info_[slot].configured||AccountAuthPolicy::active(info_[slot].authState));}
    bool any=false;for(uint8_t slot=0;slot<5;++slot)if(selected[slot]){any=disconnect(slot)||any;}return any;
}
void CalendarSyncService::accountChanged(uint8_t slot){
    if(slot>=5)return;Guard lock(mutex_);restored_[slot]=false;phonebookEnabled_[slot]=false;
}
void CalendarSyncService::finishLogin(const Job& job,AccountAuthState state,const char* message){
    Guard lock(mutex_);if(job.generation!=generation_[job.slot])return;clearSession(job.slot);
    if(info_[job.slot].connected&&job.operation!=Operation::Sync)info_[job.slot].purpose=previousPurpose_[job.slot];
    info_[job.slot].authState=state;info_[job.slot].message=message;
}
bool CalendarSyncService::current(const Job& job){
    Guard lock(mutex_);const bool login=job.operation==Operation::Poll||job.operation==Operation::Callback;
    return AccountAuthPolicy::current(generation_[job.slot],job.generation,login?sessions_[job.slot].expires:0,millis());
}
void CalendarSyncService::publish(const Job& job,const char* message,bool connected){
    Guard lock(mutex_);if(job.generation!=generation_[job.slot])return;
    info_[job.slot].message=message;info_[job.slot].connected=connected;
    if(connected)info_[job.slot].authState=AccountAuthState::Connected;
    else if(job.operation==Operation::Connect||job.operation==Operation::Poll||job.operation==Operation::Callback||job.operation==Operation::Sync)
        info_[job.slot].authState=AccountAuthState::Error;
}
bool CalendarSyncService::enqueue(uint8_t slot,Operation op,const String& code){
    if(slot>=5||!mutex_)return false;Guard lock(mutex_);if(pending_[slot])return false;
    auto* job=new(std::nothrow) Job{slot,op,generation_[slot],code,info_[slot].provider,info_[slot].purpose,identify_[slot]};if(!job)return false;
    job->rangeFrom=rangeFrom_[slot];job->rangeTo=rangeTo_[slot];
    pending_[slot]=true;info_[slot].working=true;
    // Slow syncs reuse the existing transfer class. Login/control jobs remain
    // interactive. Per-account pending + generation isolate concurrent owners.
    // OAuth can block for seconds too. Keep printer control's lane free.
    const bool queued=op==Operation::Restore||op==Operation::Disconnect
        ?NetworkWorker::instance().submitLocal(run,job,"calendar-store")
        :NetworkWorker::instance().submitTransfer(run,job,"calendar-network");
    if(!queued){
        pending_[slot]=false;info_[slot].working=false;wipe(job->code);delete job;return false;
    }return true;
}
void CalendarSyncService::run(void* pointer){
    std::unique_ptr<Job> job(static_cast<Job*>(pointer));auto& service=instance();
    if(service.current(*job))service.execute(*job);wipe(job->code);
    service.state_=NetworkService::instance().online()?CalendarSyncState::Idle:CalendarSyncState::Offline;
    Guard lock(service.mutex_);if(job->generation==service.generation_[job->slot]){
        service.pending_[job->slot]=false;service.info_[job->slot].working=false;
        publishLuaEvent(nullptr,"calendar.updated"); // Loading-state transition, no content-revision bump.
    }
}
bool CalendarSyncService::connect(uint8_t slot){
    SecurityWriteGuard security;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    if(slot>=5)return false;Guard lock(mutex_);if(!loaded_[slot]||pending_[slot]||deleting_[slot])return false;
    ++generation_[slot];clearSession(slot);
    previousPurpose_[slot]=info_[slot].purpose;info_[slot].purpose=phonebookEnabled_[slot]&&AccountAuthPolicy::hasContacts(info_[slot].purpose)?OAuthPurpose::CalendarContacts:OAuthPurpose::Calendar;identify_[slot]=false;
    info_[slot].authState=AccountAuthState::RequestingCode;info_[slot].message="Anmeldung wird vorbereitet.";
    if(enqueue(slot,Operation::Connect))return true;
    info_[slot].authState=AccountAuthState::Error;info_[slot].message="Dienst beschäftigt. Erneut versuchen.";return false;
}
bool CalendarSyncService::sync(uint8_t slot){
    if(slot>=5)return false;Guard lock(mutex_);
    if(!loaded_[slot]||!cloudEnabled_[slot]||deleting_[slot]||davConfigPending_[slot])return false;
    if(sessions_[slot].expires&&!due(sessions_[slot].expires))return false;
    return enqueue(slot,Operation::Sync);
}
bool CalendarSyncService::disconnect(uint8_t slot){
    SecurityWriteGuard security;
    if(slot>=5||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    invalidate(slot);{Guard lock(mutex_);deleting_[slot]=true;restored_[slot]=true;
        info_[slot].message="Lokale Verbindung wird entfernt.";info_[slot].working=true;}
    enqueue(slot,Operation::Disconnect);return true; // Deletion latch retries if the queue is full.
}
void CalendarSyncService::invalidate(uint8_t slot){
    SecurityWriteGuard security;
    if(slot>=5||!mutex_)return;Guard lock(mutex_);++generation_[slot];pending_[slot]=false;
    clearSession(slot);info_[slot].connected=false;info_[slot].authState=AccountAuthState::Idle;info_[slot].working=false;
    info_[slot].contactsConnected=false;info_[slot].davCalendarConnected=false;info_[slot].contactCount=0;info_[slot].contactsTruncated=false;
    restored_[slot]=false;identify_[slot]=false;
    free(events_[slot]);events_[slot]=nullptr;free(details_[slot]);details_[slot]=nullptr;
    clearAccess(slot);
    rangeFrom_[slot]=rangeTo_[slot]=0;calendarDataChanged();
}
void CalendarSyncService::calendarDataChanged(){++dataRevision_;publishLuaEvent(nullptr,"calendar.updated");}
bool CalendarSyncService::completeGoogle(const String& state,const String& code){
    if(state.length()!=64||!code.length()||code.length()>2048)return false;
    Guard lock(mutex_);
    for(uint8_t slot=0;slot<5;++slot){
        auto& session=sessions_[slot];
        if(session.provider!=EmailProvider::Google||!OAuthProtocol::callbackAllowed(session.state,state,session.expires,millis(),session.consumed,pending_[slot]))continue;
        if(!enqueue(slot,Operation::Callback,code))return false;session.consumed=true;return true;
    }return false;
}
size_t CalendarSyncService::copyEvents(uint8_t slot,EventRecord* out,size_t capacity){
    if(slot>=5||!out)return 0;Guard lock(mutex_);if(!cloudEnabled_[slot]||!events_[slot])return 0;
    size_t count=std::min(capacity,size_t(info_[slot].eventCount));memcpy(out,events_[slot],count*sizeof(EventRecord));return count;
}
bool CalendarSyncService::copyEventDetails(uint8_t slot,uint64_t id,EventDetails& out){
    out={};if(slot>=5||!id)return false;Guard lock(mutex_);
    if(!cloudEnabled_[slot]||!details_[slot]||!events_[slot])return false;
    for(uint32_t i=0;i<details_[slot]->count;++i)if(details_[slot]->items[i].localId==id){out=details_[slot]->items[i];return true;}return false;
}
void CalendarSyncService::backgroundTick(){
    if(!mutex_||uint32_t(millis()-lastTick_)<250)return;lastTick_=millis();publishAccountEvents();
    // Cloud commits must not block the main orchestration loop.
    Guard lock(mutex_,0);if(!lock)return;
    for(uint8_t slot=0;slot<5;++slot){
        if(access_[slot]&&due(accessUntil_[slot]))clearAccess(slot);
        auto& session=sessions_[slot];
        if(session.expires&&due(session.expires)){
            ++generation_[slot];pending_[slot]=false;clearSession(slot);
            if(info_[slot].connected)info_[slot].purpose=previousPurpose_[slot];
            info_[slot].authState=AccountAuthState::Expired;info_[slot].working=false;info_[slot].message="Anmeldung abgelaufen. Erneut versuchen.";
        }
        if(pending_[slot])continue;
        if(deleting_[slot]){if(due(deleteRetry_[slot])){deleteRetry_[slot]=millis()+1000;enqueue(slot,Operation::Disconnect);}return;}
        if(!restored_[slot]){if(enqueue(slot,Operation::Restore))restored_[slot]=true;return;}
        if(!cloudEnabled_[slot])continue;
        if(davConfigured_[slot]&&!info_[slot].connected&&cloudStatus_[slot]==CloudAccountState::ServerUnavailable&&due(nextSync_[slot])){
            nextSync_[slot]=millis()+60000;if(NetworkService::instance().online()&&TimeService::instance().valid()&&!enqueue(slot,Operation::Sync))nextSync_[slot]=millis()+5000;continue;
        }
        if(session.expires){
            if(session.deviceCode.length()&&due(session.next)){
                if(NetworkService::instance().online()&&TimeService::instance().valid()&&enqueue(slot,Operation::Poll))info_[slot].authState=AccountAuthState::Polling;
                return;
            }continue;
        }
        if(info_[slot].connected&&due(nextSync_[slot])){
            nextSync_[slot]=millis()+300000;
            if(NetworkService::instance().online()&&(AccountAuthPolicy::hasCalendar(info_[slot].purpose)||(phonebookEnabled_[slot]&&AccountAuthPolicy::hasContacts(info_[slot].purpose))||!access_[slot])&&!enqueue(slot,Operation::Sync))nextSync_[slot]=millis()+5000;return;
        }
    }
}
void CalendarSyncService::execute(Job& job){
    const uint8_t slot=job.slot;
    if(job.operation==Operation::Restore){loadSetups();loadDav(slot);}
    if(job.operation==Operation::Disconnect){
        SecurityWriteGuard security;if(!current(job))return;
        bool ok=CredentialStore::instance().removeOAuthRecord(tokenKey(slot).c_str());
        const String davKey=String("dav_")+slot;
        if(!CredentialStore::instance().removeOAuthRecord(davKey.c_str()))ok=false;
        if(davConfigured_[slot]&&!CredentialStore::instance().removeEmailRecord(slot))ok=false;
        if(forgetAccount_[slot]){
            if(!CredentialStore::instance().removeEmailRecord(slot))ok=false;
            for(const String& path:{String("/accounts/dav_")+slot+".bin",String("/accounts/account_")+slot+".json"})
                for(const char* suffix:{"",".bak",".tmp"})
                    if(StorageService::instance().exists(StorageArea::Config,path+suffix)&&!StorageService::instance().remove(StorageArea::Config,path+suffix))ok=false;
            for(const char* ending:{".json","_remote.json"}){const String book=String("/accounts/phonebook_account-")+slot+ending;
            for(const char* suffix:{"",".bak",".tmp"})if(StorageService::instance().exists(StorageArea::UserData,book+suffix)&&!StorageService::instance().remove(StorageArea::UserData,book+suffix))ok=false;}
        }
        for(const char* suffix:{"",".bak",".tmp"}){
            const String path=snapshotPath(slot)+suffix;
            if(StorageService::instance().exists(StorageArea::UserData,path)&&!StorageService::instance().remove(StorageArea::UserData,path))ok=false;
            const String detailPath=detailSnapshotPath(slot)+suffix;
            if(StorageService::instance().exists(StorageArea::UserData,detailPath)&&!StorageService::instance().remove(StorageArea::UserData,detailPath))ok=false;
        }
        const auto account=EmailAccountService::instance().account(slot);
        {Guard lock(mutex_);if(current(job)){
            deleting_[slot]=!ok;restored_[slot]=true;loaded_[slot]=true;
            info_[slot].configured=account.configured;info_[slot].email=account.address;info_[slot].provider=account.provider;
            info_[slot].eventCount=0;info_[slot].authState=ok?AccountAuthState::Idle:AccountAuthState::Error;
            cloudStatus_[slot]=ok?CloudAccountState::Disconnected:CloudAccountState::Error;
            permissionMissing_[slot]=false;collections_[slot].clear();
            if(davConfigured_[slot]){info_[slot].configured=true;info_[slot].email=dav_[slot].user;info_[slot].provider=dav_[slot].provider==uint8_t(AccountProvider::ICloud)?EmailProvider::Apple:EmailProvider::Other;}
            if(ok&&forgetAccount_[slot]){phonebookEnabled_[slot]=false;davConfigured_[slot]=false;forgetAccount_[slot]=false;info_[slot].configured=false;info_[slot].email="";info_[slot].name="";}
        }}
        publish(job,ok?"Verbindung getrennt. Lokale Tokens und Kalendercache entfernt.":"Entfernen fehlgeschlagen. Wird erneut versucht.");return;
    }
    if(executeDav(job))return;
    auto account=EmailAccountService::instance().account(slot);
    if(job.operation==Operation::Restore){
        Guard lock(mutex_);if(!current(job))return;
        loaded_[slot]=true;info_[slot].configured=account.configured;info_[slot].provider=account.provider;info_[slot].email=account.address;
        if(!account.configured){info_[slot].message="Nicht verbunden.";return;}
    }
    if(account.configured&&account.provider!=job.provider&&job.operation==Operation::Connect){
        publish(job,"Dieser Kontoplatz gehört zu einem anderen Anbieter.");return;
    }
    const EmailProvider provider=account.configured?account.provider:job.provider;
    if(!supported(provider)){publish(job,"Für diesen Anbieter ist noch keine OAuth-Anmeldung verfügbar.");return;}
    if(!account.configured&&!(job.operation==Operation::Connect||job.operation==Operation::Poll||job.operation==Operation::Callback)){
        publish(job,"Nicht verbunden.");return;
    }
    HeapErased<TokenRecord> token;if(!token){publish(job,"Nicht genügend Autorisierungsspeicher.");return;}
    const bool saved=tokenRecord(slot,*token);
    if(job.operation==Operation::Restore||job.operation==Operation::Sync){
        if(saved)job.purpose=token->version==2?AccountAuthPolicy::fromTag(token->provider>>4):OAuthPurpose::Calendar;
    }
    Erased<ProviderRecord> config;
    if(!providerRecord(provider,config.value,job.purpose)){
        publish(job,"Für diesen Anbieter ist noch keine Anmelde-App (Client-ID) eingerichtet. Erweiterte OAuth-Einstellungen öffnen.");return;
    }
    if(provider==EmailProvider::Google&&(!config.value.secret[0]||
        (job.purpose!=OAuthPurpose::Identity&&!OAuthProtocol::httpsRedirect(config.value.redirect)))){
        publish(job,job.purpose!=OAuthPurpose::Identity?
            "Für diese Google-Funktion ist eine zusätzliche OAuth-Konfiguration erforderlich. HTTPS-Rückleitung und lokales Client-Secret einrichten.":
            "Google-Anmeldung noch nicht eingerichtet. Administrator muss das Client-Secret der Geräte-App geschützt am Display hinterlegen.");return;
    }
    const bool bound=saved&&(token->provider&15)==uint8_t(provider)&&identity(account.address,token->email)&&String(token->clientId)==config.value.clientId;
    if(job.operation==Operation::Restore){
        if(!bound){publish(job,"Nicht verbunden.");return;}
        {Guard lock(mutex_);if(!current(job))return;info_[slot].purpose=job.purpose;nextSync_[slot]=0;}
        publish(job,"Konto verbunden.",true);
        if(AccountAuthPolicy::hasCalendar(job.purpose))restoreCalendarCache(job);return;
    }
    if(!NetworkService::instance().online()||!TimeService::instance().valid()){
        if(job.operation==Operation::Poll){Guard lock(mutex_);if(current(job)){
            sessions_[slot].next=millis()+10000;info_[slot].authState=AccountAuthState::AwaitingUser;info_[slot].message="Netzwerk oder Uhrzeit fehlen. Anmeldung bleibt bis zum Ablauf offen.";
        }}else publish(job,"Internetverbindung und gültige Uhrzeit erforderlich.",info(slot).connected);return;
    }
    state_=CalendarSyncState::Syncing;
    if(job.operation==Operation::Sync){
        if(!bound){publish(job,"Erneute Anmeldung erforderlich.");return;}
        String cached;{Guard lock(mutex_);if(current(job)&&access_[slot]&&!due(accessUntil_[slot]))cached=access_[slot];}
        if(cached.length()){
            if(AccountAuthPolicy::hasCalendar(job.purpose)){discoverOAuthCalendars(job,provider,cached);fetchCalendar(job,provider,account.address,cached);}
            if(AccountAuthPolicy::hasContacts(job.purpose))fetchContacts(job,provider,account.address,cached);
            if(job.purpose==OAuthPurpose::Identity)publish(job,"Konto erfolgreich verbunden.",true);
            {Guard lock(mutex_);if(current(job))nextSync_[slot]=millis()+300000;}
            wipe(cached);return;
        }
    }
    if(job.operation==Operation::Connect){
        Session session;session.provider=provider;session.purpose=job.purpose;
        session.email=job.identify&&(!account.configured||account.oauthManaged)?String():account.address;session.expires=millis()+600000;
        String link,userCode;
        if(!AccountAuthPolicy::deviceFlow(provider,job.purpose)){
            session.state=OAuthProtocol::randomToken();session.verifier=OAuthProtocol::randomToken();
            const String challenge=OAuthProtocol::challenge(session.verifier);
            if(session.state.length()!=64||session.verifier.length()!=64||challenge.length()!=43){wipe(session.verifier);publish(job,"Anmeldung konnte nicht sicher vorbereitet werden.");return;}
            link="https://accounts.google.com/o/oauth2/v2/auth?"+form("client_id",config.value.clientId)+"&"+form("redirect_uri",config.value.redirect)+
                "&response_type=code&access_type=offline&prompt=consent&"+form("scope",requestedScope(provider,job.purpose))+"&"+form("state",session.state)+
                "&code_challenge_method=S256&"+form("code_challenge",challenge);
            if(session.email.length())link+="&"+form("login_hint",session.email);
        }else{
            const String scope=requestedScope(provider,job.purpose);
            Reply reply;String body=form("client_id",config.value.clientId)+"&"+form("scope",scope);
            const bool ok=request(provider==EmailProvider::Google?"https://oauth2.googleapis.com/device/code":
                "https://login.microsoftonline.com/common/oauth2/v2.0/devicecode",body,"",reply);wipe(body);
            SecureAllocator allocator;JsonDocument doc(&allocator);
            if(!ok||reply.status!=200||deserializeJson(doc,reply.body)){publish(job,"Anmeldung konnte nicht gestartet werden. Verbindung und OAuth-App prüfen.");return;}
            session.deviceCode=doc["device_code"]|"";userCode=doc["user_code"]|"";
            link=provider==EmailProvider::Google?String(doc["verification_url"]|""):String(doc["verification_uri"]|"");
            const int expires=doc["expires_in"]|0,interval=doc["interval"]|5;
            if(!opaqueToken(session.deviceCode,4096)||!opaqueToken(userCode,32)||!verificationUrl(provider,link)||
                expires<1||expires>3600||interval<1||interval>60){
                wipe(session.deviceCode);publish(job,"Ungültige Antwort der offiziellen Anmeldeseite.");return;
            }
            session.interval=uint32_t(interval)*1000;session.expires=millis()+uint32_t(expires)*1000;session.next=millis()+session.interval;
        }
        Guard lock(mutex_);if(!current(job)){wipe(session.deviceCode);wipe(session.verifier);return;}
        clearSession(slot);sessions_[slot]=session;wipe(session.deviceCode);wipe(session.verifier);
        info_[slot].authorizationUrl=link;info_[slot].userCode=userCode;info_[slot].purpose=job.purpose;
        info_[slot].authState=AccountAuthState::AwaitingUser;info_[slot].message="Offizielle Anmeldeseite öffnen, anmelden und Zugriff bestätigen. Diese Seite offen lassen.";return;
    }
    String body=form("client_id",config.value.clientId);
    if(provider==EmailProvider::Google)body+="&"+form("client_secret",config.value.secret);
    if(job.operation==Operation::Sync)body+="&grant_type=refresh_token&"+form("refresh_token",token->refresh);
    else{
        Guard lock(mutex_);auto& session=sessions_[slot];if(!current(job)||!session.expires){wipe(body);return;}
        if(session.provider!=provider||session.purpose!=job.purpose||(!session.email.length()&&!job.identify)||
            (session.email.length()&&!identity(account.address,session.email))){wipe(body);return;}
        if(job.operation==Operation::Poll){
            body+="&grant_type=urn:ietf:params:oauth:grant-type:device_code&"+form("device_code",session.deviceCode);session.next=millis()+session.interval;
        }else body+="&grant_type=authorization_code&"+form("code",job.code)+"&"+form("redirect_uri",config.value.redirect)+"&"+form("code_verifier",session.verifier);
    }
    Reply reply;const bool ok=request(provider==EmailProvider::Google?"https://oauth2.googleapis.com/token":msToken,body,"",reply);wipe(body);
    SecureAllocator allocator;JsonDocument doc(&allocator);
    if(!ok||deserializeJson(doc,reply.body)){
        if(job.operation==Operation::Poll){Guard lock(mutex_);if(current(job)){
            auto& session=sessions_[slot];session.interval=std::min(uint32_t(60000),session.interval*2);session.next=millis()+session.interval;
            info_[slot].authState=AccountAuthState::AwaitingUser;info_[slot].message="Verbindung unterbrochen. Anmeldung wird erneut geprüft.";
        }}else finishLogin(job,AccountAuthState::Error,"Netzwerkfehler. Erneut versuchen.");return;
    }
    const String error=doc["error"]|"";
    if(job.operation==Operation::Poll&&(error=="authorization_pending"||error=="slow_down")){
        Guard lock(mutex_);if(current(job)){
            auto& session=sessions_[slot];session.interval=OAuthProtocol::pollDelay(session.interval,error=="slow_down");session.next=millis()+session.interval;
            info_[slot].authState=AccountAuthState::AwaitingUser;info_[slot].message="Warte auf Anmeldung und Bestätigung beim Anbieter.";
        }return;
    }
    if(reply.status!=200){
        if(error=="invalid_grant"){Guard lock(mutex_);if(current(job))cloudStatus_[slot]=CloudAccountState::NeedsLogin;}
        if(job.operation==Operation::Sync&&error=="invalid_grant"){
            SecurityWriteGuard security;if(current(job))CredentialStore::instance().removeOAuthRecord(tokenKey(slot).c_str());
        }
        finishLogin(job,error=="expired_token"?AccountAuthState::Expired:(error=="access_denied"||error=="authorization_declined")?AccountAuthState::Cancelled:AccountAuthState::Error,
            error=="expired_token"?"Anmeldung abgelaufen. Erneut versuchen.":(error=="access_denied"||error=="authorization_declined")?"Anmeldung beim Anbieter abgebrochen.":
                error=="invalid_grant"?"Autorisierung abgelaufen oder widerrufen. Erneut verbinden.":"Anmeldung fehlgeschlagen. OAuth-App und Freigabe prüfen.");
        if(job.operation==Operation::Sync){Guard lock(mutex_);if(current(job)){info_[slot].connected=false;clearAccess(slot);}}
        return;
    }
    String access=doc["access_token"]|"",refresh=doc["refresh_token"]|"",type=doc["token_type"]|"";type.toLowerCase();
    if(!refresh.length()&&job.operation==Operation::Sync)refresh=token->refresh;
    const int expires=doc["expires_in"]|0;String scope=doc["scope"]|"";scope.toLowerCase();
    const char* needed=provider==EmailProvider::Google?(AccountAuthPolicy::hasCalendar(job.purpose)?"https://www.googleapis.com/auth/calendar.events.readonly":"email"):
        AccountAuthPolicy::hasCalendar(job.purpose)?"calendars.read":"user.read";
    const char* alias=provider==EmailProvider::Google?"https://www.googleapis.com/auth/userinfo.email":AccountAuthPolicy::hasCalendar(job.purpose)?"https://graph.microsoft.com/calendars.read":"https://graph.microsoft.com/user.read";
    const bool contactsMissing=AccountAuthPolicy::hasContacts(job.purpose)&&!AccountAuthPolicy::scopeHas(scope,provider==EmailProvider::Google?"https://www.googleapis.com/auth/contacts.readonly":"contacts.read",provider==EmailProvider::Microsoft?"https://graph.microsoft.com/contacts.read":nullptr);
    const bool missing=(scope.length()&&!AccountAuthPolicy::scopeHas(scope,needed,alias))||contactsMissing;
    if(missing){Guard lock(mutex_);if(current(job)){permissionMissing_[slot]=true;missingCapability_[slot]=contactsMissing?AccountCapability::Contacts:AccountAuthPolicy::hasCalendar(job.purpose)?AccountCapability::Calendar:AccountCapability::Profile;}}
    if(type!="bearer"||expires<1||expires>86400||!opaqueToken(access,8192)||!opaqueToken(refresh,4096)||missing){
        wipe(access);wipe(refresh);finishLogin(job,AccountAuthState::Error,"Berechtigung oder dauerhafte Autorisierung fehlt.");return;
    }
    const String expected=job.operation!=Operation::Sync&&job.identify&&account.oauthManaged?String():account.address;
    if(acceptTokens(job,provider,expected,config.value.clientId,access,refresh)){
        if(expires>60){Guard lock(mutex_);if(current(job)){
            clearAccess(slot);access_[slot]=static_cast<char*>(heap_caps_malloc(access.length()+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
            if(access_[slot]){memcpy(access_[slot],access.c_str(),access.length()+1);accessUntil_[slot]=millis()+uint32_t(expires-60)*1000;}
        }}
        if(AccountAuthPolicy::hasCalendar(job.purpose)){discoverOAuthCalendars(job,provider,access);fetchCalendar(job,provider,info(slot).email,access);}
        if(AccountAuthPolicy::hasContacts(job.purpose))fetchContacts(job,provider,info(slot).email,access);
        {Guard lock(mutex_);if(current(job))nextSync_[slot]=millis()+300000;}
    }wipe(access);wipe(refresh);state_=CalendarSyncState::Idle;
}
bool CalendarSyncService::acceptTokens(Job& job,EmailProvider provider,const String& expected,const char* id,String& access,String& refresh){
    String email=expected,name;
    if(job.operation!=Operation::Sync){
        Reply reply;const bool ok=request(provider==EmailProvider::Google?"https://openidconnect.googleapis.com/v1/userinfo":
            "https://graph.microsoft.com/v1.0/me?$select=mail,userPrincipalName,displayName","",access,reply);
        SecureAllocator allocator;JsonDocument doc(&allocator);
        if(!ok||reply.status!=200||deserializeJson(doc,reply.body)){finishLogin(job,AccountAuthState::Error,"Kontoinhaber konnte nicht überprüft werden.");return false;}
        email=provider==EmailProvider::Google?String(doc["email"]|""):String(doc["mail"]|"");
        if(provider==EmailProvider::Microsoft&&!email.length())email=doc["userPrincipalName"]|"";
        name=provider==EmailProvider::Google?String(doc["name"]|""):String(doc["displayName"]|"");if(name.length()>96)name="";
        if(!email.length()||email.length()>254||(expected.length()&&!identity(expected,email))||
            (provider==EmailProvider::Google&&!(doc["email_verified"]|false))){
            finishLogin(job,AccountAuthState::Error,"Falsches oder nicht bestätigtes Konto angemeldet.");return false;
        }
    }
    SecurityWriteGuard security;if(!current(job))return false;
    {Guard lock(mutex_);if(job.operation!=Operation::Sync&&AccountAuthPolicy::hasContacts(job.purpose)&&!phonebookEnabled_[job.slot]){
       finishLogin(job,AccountAuthState::Cancelled,"Telefonbuch ausgeschaltet; Kontaktfreigabe verworfen.");return false;}}
    if(job.operation!=Operation::Sync&&(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())){
        finishLogin(job,AccountAuthState::Error,"Einstellungen erneut entsperren und Anmeldung neu starten.");return false;
    }
    if(job.identify&&job.operation!=Operation::Sync&&!EmailAccountService::instance().storeOAuthIdentity(job.slot,provider,email)){
        finishLogin(job,AccountAuthState::Error,"Kontoprofil konnte nicht sicher gespeichert werden.");return false;
    }
    const auto account=EmailAccountService::instance().account(job.slot);
    if(!account.configured||account.provider!=provider||!identity(account.address,email))return false;
    if(job.operation!=Operation::Sync){Guard lock(mutex_);
        clearAccess(job.slot);free(events_[job.slot]);events_[job.slot]=nullptr;free(details_[job.slot]);details_[job.slot]=nullptr;info_[job.slot].eventCount=0;
        info_[job.slot].connected=false;info_[job.slot].email=account.address;
    }
    HeapErased<TokenRecord> token;if(!token){publish(job,"Nicht genügend Autorisierungsspeicher.");return false;}
    token->version=2;token->provider=uint8_t(provider)|(AccountAuthPolicy::purposeTag(job.purpose)<<4);
    strlcpy(token->email,email.c_str(),sizeof(token->email));strlcpy(token->clientId,id,sizeof(token->clientId));strlcpy(token->refresh,refresh.c_str(),sizeof(token->refresh));
    if(!CredentialStore::instance().writeOAuthRecord(tokenKey(job.slot).c_str(),&*token,sizeof(*token))){
        finishLogin(job,AccountAuthState::Error,"Autorisierung konnte nicht sicher gespeichert werden.");return false;
    }
    {Guard lock(mutex_);clearSession(job.slot);info_[job.slot].email=email;info_[job.slot].name=name;
    info_[job.slot].provider=provider;info_[job.slot].configured=true;info_[job.slot].purpose=job.purpose;loaded_[job.slot]=true;
    permissionMissing_[job.slot]=false;
    cloudStatus_[job.slot]=CloudAccountState::Connected;
    publish(job,"Konto erfolgreich verbunden.",true);}
    persistCloudMetadata(job.slot);return true;
}
bool CalendarSyncService::requestRange(int64_t from,int64_t to){
    if(from<=0||to<=from||to-from>366LL*86400||!mutex_)return false;
    Guard lock(mutex_,0);if(!lock)return false;bool changed=false;
    for(unsigned slot=0;slot<5;++slot)if(cloudEnabled_[slot]&&info_[slot].configured&&
        (rangeFrom_[slot]!=from||rangeTo_[slot]!=to)){
        rangeFrom_[slot]=from;rangeTo_[slot]=to;nextSync_[slot]=0;changed=true;
    }
    return changed;
}
bool CalendarSyncService::discoverOAuthCalendars(Job& job,EmailProvider provider,const String& access){
 const RequestDeadline deadline(millis(),5000);const bool google=provider==EmailProvider::Google;
 const String base=google?String("https://www.googleapis.com/calendar/v3/users/me/calendarList?maxResults=16&fields=nextPageToken,items(id,summary,accessRole,deleted)"):String("https://graph.microsoft.com/v1.0/me/calendars?$top=16&$select=id,name");
 String url=base;std::vector<CloudCalendarCollection> found;
 for(unsigned page=0;page<4;++page){
  Reply reply;if(!current(job)||!deadline.remaining(millis())||!request(url.c_str(),"",access,reply,deadline.stepTimeout(millis(),5000))||reply.status!=200)return false;
  JsonDocument doc;if(deserializeJson(doc,reply.body))return false;auto rows=google?doc["items"].as<JsonArrayConst>():doc["value"].as<JsonArrayConst>();if(rows.isNull()||rows.size()>16)return false;
  for(JsonObjectConst row:rows){if(row["deleted"]|false)continue;const char* id=row["id"]|"";const char* name=google?(row["summary"]|""):(row["name"]|"");
   if(!id[0]||strlen(id)>1024||strlen(name)>192||found.size()>=16)return false;
   for(const auto& prior:found)if(prior.id==id)return false;found.push_back({id,name,true});
  }
  const String next=google?String(doc["nextPageToken"]|""):String(doc["@odata.nextLink"]|"");
  if(!next.length()){Guard lock(mutex_);if(!current(job))return false;collections_[job.slot]=std::move(found);return true;}
  if(google){if(next.length()>2048)return false;url=base+"&pageToken="+OAuthProtocol::encode(next);}
  else {if(next.length()>4096||!next.startsWith("https://graph.microsoft.com/v1.0/me/calendars?")||next.indexOf('@')>=0||next.indexOf('\\')>=0||next.indexOf('#')>=0)return false;for(size_t i=0;i<next.length();++i)if(uint8_t(next[i])<=32||uint8_t(next[i])>=127)return false;url=next;}
 }
 return false;
}
bool CalendarSyncService::fetchCalendar(Job& job,EmailProvider provider,const String& email,const String& access){
    const RequestDeadline deadline(millis(),15000);
    const int64_t now=TimeService::instance().nowUtc();
    const String start=OAuthProtocol::utcText(job.rangeFrom?job.rangeFrom:now-86400),end=OAuthProtocol::utcText(job.rangeTo?job.rangeTo:now+30*86400);
    auto selected=selectedOAuthCalendars(job,provider,email);
    auto* events=static_cast<EventRecord*>(heap_caps_calloc(kCloudCalendarEvents,sizeof(EventRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* details=static_cast<EventDetailsSnapshot*>(heap_caps_malloc(sizeof(EventDetailsSnapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!events||!details){free(events);free(details);publish(job,"Nicht genügend Kalenderspeicher.",true);return false;}new(details) EventDetailsSnapshot{};
    uint8_t count=0;bool valid=true,truncated=false;
    for(const auto& collection:selected){
    if(!collection.id.length()){valid=false;break;}
    if(count==kCloudCalendarEvents){truncated=true;break;}
    const String path=provider==EmailProvider::Google?String("https://www.googleapis.com/calendar/v3/calendars/")+OAuthProtocol::encode(collection.id)+"/events?":
        collection.id=="primary"?String("https://graph.microsoft.com/v1.0/me/calendarView?"):String("https://graph.microsoft.com/v1.0/me/calendars/")+OAuthProtocol::encode(collection.id)+"/calendarView?";
    String url=provider==EmailProvider::Google?
        path+"singleEvents=true&orderBy=startTime&maxResults=32&"+
        form("timeMin",start)+"&"+form("timeMax",end)+"&"+form("fields","nextPageToken,items(id,status,summary,description,location,start,end)"):
        path+"$top=32&$orderby=start/dateTime&$select=id,subject,body,start,end,isAllDay,location&"+
        form("startDateTime",start)+"&"+form("endDateTime",end);
    const String baseUrl=url;
    Reply reply;if(!current(job)||!request(url.c_str(),"",access,reply,deadline.stepTimeout(millis(),10000))||reply.status!=200){
        if(reply.status==403){Guard lock(mutex_);if(current(job)){permissionMissing_[job.slot]=true;missingCapability_[job.slot]=AccountCapability::Calendar;}}
        if(reply.status==401){Guard lock(mutex_);if(current(job))clearAccess(job.slot);}
        publish(job,"Verbunden, Kalenderabruf fehlgeschlagen. Automatischer Wiederholungsversuch.",true);valid=false;break;
    }
    SecureAllocator allocator;JsonDocument doc(&allocator);
    if(deserializeJson(doc,reply.body)){publish(job,"Ungültige Kalenderantwort. Letzten Cache behalten.",true);valid=false;break;}
    JsonArrayConst items=provider==EmailProvider::Google?doc["items"].as<JsonArrayConst>():doc["value"].as<JsonArrayConst>();
    if(items.isNull()||items.size()>32){publish(job,"Ungültige Kalenderliste.",true);valid=false;break;}
    for(unsigned page=0;page<16;++page){
    if(!current(job)||!NetworkService::instance().online()||!deadline.remaining(millis())){valid=false;break;}
    for(JsonObjectConst item:items){
        if(String(item["status"]|"")=="cancelled")continue;
        if(count==kCloudCalendarEvents){truncated=true;break;}
        const char* id=item["id"]|"";
        const char* s=item["start"]["dateTime"]|"";const char* e=item["end"]["dateTime"]|"";
        bool allDay=item["isAllDay"]|false;
        if(provider==EmailProvider::Google&&!s[0]){s=item["start"]["date"]|"";e=item["end"]["date"]|"";allDay=true;}
        const int64_t begin=OAuthProtocol::utcDate(s),finish=OAuthProtocol::utcDate(e);
        if(!id[0]||!begin||finish<=begin){valid=false;break;}
        auto& event=events[count++];event.startUtc=begin;event.endUtc=finish;
        uint64_t hash=14695981039346656037ULL;for(const char* p=id;*p;++p){hash^=uint8_t(*p);hash*=1099511628211ULL;}
        if(collection.id!="primary"){hash*=1099511628211ULL;for(const char* p=collection.id.c_str();*p;++p){hash^=uint8_t(*p);hash*=1099511628211ULL;}}
        event.localId=hash^(uint64_t(job.slot+1)<<56);event.calendarId=0x80000000u+job.slot;
        for(unsigned i=0;i+1<count;++i)if(events[i].localId==event.localId){valid=false;break;}
        if(!valid)break;
        event.flags=EventReadOnly|EventExternal|(allDay?EventAllDay:0);
        strlcpy(event.uid,id,sizeof(event.uid));
        utf8Copy(event.title,sizeof(event.title),provider==EmailProvider::Google?(item["summary"]|""):(item["subject"]|""));
        utf8Copy(event.location,sizeof(event.location),provider==EmailProvider::Google?(item["location"]|""):(item["location"]["displayName"]|""));
        auto& detail=details->items[count-1];detail.localId=event.localId;
        auto copy=[&detail](char* target,size_t capacity,const char* value){detail.truncated|=strlen(value)>=capacity;utf8Copy(target,capacity,value);};
        copy(detail.location,sizeof(detail.location),provider==EmailProvider::Google?(item["location"]|""):(item["location"]["displayName"]|""));
        copy(detail.notes,sizeof(detail.notes),provider==EmailProvider::Google?(item["description"]|""):(item["body"]["content"]|""));
        detail.notesHtml=provider==EmailProvider::Microsoft&&String(item["body"]["contentType"]|"")=="html";
        if(provider==EmailProvider::Microsoft){auto address=item["location"]["address"];
            copy(detail.street,sizeof(detail.street),address["street"]|"");copy(detail.city,sizeof(detail.city),address["city"]|"");
            copy(detail.postalCode,sizeof(detail.postalCode),address["postalCode"]|"");copy(detail.region,sizeof(detail.region),address["state"]|"");copy(detail.country,sizeof(detail.country),address["countryOrRegion"]|"");
        }
        utf8Copy(event.notes,sizeof(event.notes),detail.notes);
    }
    if(!valid)break;
    const String next=provider==EmailProvider::Google?String(doc["nextPageToken"]|""):String(doc["@odata.nextLink"]|"");
    if(!next.length())break;
    if(count==kCloudCalendarEvents||page==15){truncated=true;break;}
    if(provider==EmailProvider::Google){
        if(next.length()>2048){valid=false;break;}url=baseUrl+"&"+form("pageToken",next);
    }else{
        // Never send a bearer token to a host/path supplied by an untrusted
        // nextLink. Retain the original account, provider and requested horizon.
        if(next.length()>4096||!next.startsWith(path)||
           next.indexOf('@')>=0||next.indexOf('\\')>=0||next.indexOf('#')>=0){valid=false;break;}
        for(size_t i=0;i<next.length();++i)if(uint8_t(next[i])<=32||uint8_t(next[i])>=127){valid=false;break;}
        if(!valid)break;url=next;
    }
    Reply following;
    if(!current(job)||!request(url.c_str(),"",access,following,deadline.stepTimeout(millis(),10000))||following.status!=200||
       deserializeJson(doc,following.body)){valid=false;break;}
    items=provider==EmailProvider::Google?doc["items"].as<JsonArrayConst>():doc["value"].as<JsonArrayConst>();
    if(items.isNull()||items.size()>32){valid=false;break;}
    }
    if(!valid)break;
    }
    SecurityWriteGuard security;
    const auto account=EmailAccountService::instance().account(job.slot);
    if(!valid||!current(job)||!account.configured||account.provider!=provider||!identity(account.address,email)){
        if(!valid&&current(job))publish(job,"Kalenderfolgeseite ungueltig oder nicht erreichbar; letzter Cache bleibt erhalten.",true);
        free(details);free(events);return false;
    }
    return commitCalendarCache(job,events,details,count,truncated);
}
bool CalendarSyncService::restoreCalendarCache(Job& job){
    const uint8_t slot=job.slot;
    auto* cached=static_cast<EventRecord*>(heap_caps_calloc(1,kCloudCalendarEvents*sizeof(EventRecord)+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));size_t actual=0;
    bool valid=cached&&StorageService::instance().exists(StorageArea::UserData,snapshotPath(slot))&&
        StorageService::instance().read(StorageArea::UserData,snapshotPath(slot),cached,kCloudCalendarEvents*sizeof(EventRecord)+1,&actual)&&actual<=kCloudCalendarEvents*sizeof(EventRecord)&&actual%sizeof(EventRecord)==0;
    if(valid)for(size_t i=0;i<actual/sizeof(EventRecord);++i){const auto& e=cached[i];
        if(!e.localId||!e.calendarId||!memchr(e.title,0,sizeof(e.title))||!memchr(e.location,0,sizeof(e.location))||e.endUtc<=e.startUtc||(e.flags&(EventReadOnly|EventExternal))!=(EventReadOnly|EventExternal)){valid=false;break;}}
    auto* detail=static_cast<EventDetailsSnapshot*>(heap_caps_calloc(1,sizeof(EventDetailsSnapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));size_t detailSize=0;bool recovered=false;
    if(valid&&detail){auto validator=[&](fs::FS& fs,const String& path){auto file=fs.open(path,"r");const size_t size=file?file.size():0;return file&&EventDetailsSnapshot::compatibleSize(size)&&
        file.read(reinterpret_cast<uint8_t*>(detail),size)==size&&detail->count==actual/sizeof(EventRecord)&&detail->valid(cached,actual);};recovered=StorageService::instance().recoverAtomicWrite(StorageArea::UserData,detailSnapshotPath(slot),validator);}
    const bool detailValid=valid&&detail&&recovered&&StorageService::instance().read(StorageArea::UserData,detailSnapshotPath(slot),detail,sizeof(*detail),&detailSize)&&
        EventDetailsSnapshot::compatibleSize(detailSize)&&detail->count==actual/sizeof(EventRecord)&&detail->valid(cached,actual);
    Guard lock(mutex_);const bool accepted=current(job)&&valid&&cloudEnabled_[slot];
    if(accepted){free(events_[slot]);events_[slot]=cached;cached=nullptr;info_[slot].eventCount=actual/sizeof(EventRecord);info_[slot].truncated=info_[slot].eventCount>=kCloudCalendarEvents;
        ++dataRevision_;free(details_[slot]);details_[slot]=detailValid?detail:nullptr;if(detailValid)detail=nullptr;}
    free(detail);free(cached);return accepted;
}
bool CalendarSyncService::commitCalendarCache(Job& job,EventRecord* events,EventDetailsSnapshot* details,uint8_t count,bool truncated){
    SecurityWriteGuard security;
    if(!events||!details||count>kCloudCalendarEvents||!current(job)){free(details);free(events);return false;}
    // Replace, never append: recurring sync must not duplicate appointments.
    details->count=count;details->eventsChecksum=detailChecksum(events,count*sizeof(EventRecord));details->detailsChecksum=detailChecksum(details->items,count*sizeof(EventDetails));
    bool changed=true;
    {Guard lock(mutex_);const auto* previous=details_[job.slot];
        changed=!previous||previous->count!=count||previous->eventsChecksum!=details->eventsChecksum||
            previous->detailsChecksum!=details->detailsChecksum;
    }
    if(changed&&(!StorageService::instance().writeAtomic(StorageArea::UserData,detailSnapshotPath(job.slot),details,EventDetailsSnapshot::storedBytes(count))||
       !StorageService::instance().writeAtomic(StorageArea::UserData,snapshotPath(job.slot),events,count*sizeof(EventRecord)))){
        free(details);free(events);publish(job,"Kalendercache konnte nicht gespeichert werden.",true);return false;
    }
    Guard lock(mutex_);if(!current(job)){free(details);free(events);return false;}
    free(events_[job.slot]);events_[job.slot]=events;
    free(details_[job.slot]);details_[job.slot]=details;
    auto& status=info_[job.slot];status.eventCount=count;status.lastSyncMs=millis();
    status.truncated=truncated;
    nextSync_[job.slot]=(job.rangeFrom==rangeFrom_[job.slot]&&job.rangeTo==rangeTo_[job.slot])?millis()+300000:0;
    publish(job,status.truncated?"Kalender unvollstaendig: 128-Termine-/Seitenlimit erreicht. Zeitraum verkleinern.":"Kalenderzeitraum synchronisiert.",true);
    if(changed)calendarDataChanged();
    return true;
}
bool CalendarSyncService::fetchContacts(Job& job,EmailProvider provider,const String& email,const String& access){
    {Guard lock(mutex_);if(!current(job)||!phonebookEnabled_[job.slot]||!cloudEnabled_[job.slot])return false;}
    auto fail=[&](const char* message,bool permission=false){Guard lock(mutex_);if(current(job)){
        info_[job.slot].contactsMessage=message;info_[job.slot].contactsConnected=false;
        if(permission){permissionMissing_[job.slot]=true;missingCapability_[job.slot]=AccountCapability::Contacts;}}return false;};
    const String googleBase="https://people.googleapis.com/v1/people/me/connections?pageSize=50&personFields=names,emailAddresses,phoneNumbers,addresses,biographies,birthdays,organizations,metadata&sources=READ_SOURCE_TYPE_CONTACT";
    String url=provider==EmailProvider::Google?googleBase:String("https://graph.microsoft.com/v1.0/me/contacts?$top=50&$select=id,displayName,emailAddresses,businessPhones,homePhones,mobilePhone,homeAddress,businessAddress,otherAddress,personalNotes,birthday,companyName,jobTitle");
    std::vector<PhonebookContact> contacts;contacts.reserve(128);bool truncated=false;
    for(unsigned page=0;page<4;++page){
        {Guard lock(mutex_);if(!current(job)||!phonebookEnabled_[job.slot]||!cloudEnabled_[job.slot])return false;}
        Reply reply;if(!request(url.c_str(),"",access,reply)||reply.status!=200)return fail(reply.status==403?"Kontaktfreigabe fehlt. Telefonbuch im Browser freigeben.":"Kontaktabruf fehlgeschlagen; letzter Offline-Stand bleibt erhalten.",reply.status==403);
        SecureAllocator allocator;JsonDocument doc(&allocator);if(deserializeJson(doc,reply.body))return fail("Ungültige Kontaktantwort; Offline-Stand bleibt erhalten.");
        auto rows=provider==EmailProvider::Google?doc["connections"].as<JsonArrayConst>():doc["value"].as<JsonArrayConst>();
        // Google's empty connections array may be omitted for an empty address book.
        if(rows.isNull()&&!(provider==EmailProvider::Google&&doc.is<JsonObject>()&&doc["connections"].isNull()))return fail("Ungültige Kontaktliste.");
        if(rows.size()>50)return fail("Kontaktantwort überschreitet das Seitenlimit.");
        for(JsonObjectConst row:rows){
            if(provider==EmailProvider::Google&&(row["metadata"]["deleted"]|false))continue;
            const char* id=provider==EmailProvider::Google?(row["resourceName"]|""):(row["id"]|"");if(!*id)return fail("Kontakt ohne eindeutige Kennung abgelehnt.");
            if(contacts.size()==128){truncated=true;break;}PhonebookContact contact;uint64_t hash=14695981039346656037ULL;
            for(const char* p=id;*p;++p){hash^=uint8_t(*p);hash*=1099511628211ULL;}char key[40];snprintf(key,sizeof(key),"cloud-%u-%08lx%08lx",unsigned(provider),(unsigned long)(hash>>32),(unsigned long)hash);contact.id=key;contact.readOnly=true;
            auto text=[&](JsonVariantConst value,size_t maximum){const char* input=value.is<const char*>()?value.as<const char*>():"";
                String output=input;if(output.length()>maximum){size_t cut=maximum;while(cut&&(uint8_t(input[cut])&0xc0)==0x80)--cut;output.remove(cut);truncated=true;}return output;};
            auto label=[&](JsonVariantConst type){const String raw=text(type,48);return raw=="home"?String("Privat"):raw=="work"?String("Arbeit"):raw=="mobile"?String("Mobil"):raw.length()?raw:String("Sonstige");};
            auto addValue=[&](std::vector<ContactProtocol::Value>& values,const String& kind,JsonVariantConst source,size_t maximum){
                const String value=text(source,maximum);if(!value.length())return;if(values.size()>=8){truncated=true;return;}values.push_back({kind.c_str(),value.c_str()});};
            auto addAddress=[&](JsonVariantConst address,const String& kind,bool google){
                ContactProtocol::Address a;a.label=kind.c_str();a.street=text(address[google?"streetAddress":"street"],192).c_str();a.city=text(address["city"],128).c_str();
                a.postalCode=text(address["postalCode"],32).c_str();a.region=text(address[google?"region":"state"],96).c_str();a.country=text(address[google?"country":"countryOrRegion"],96).c_str();
                if(a.street.empty()&&a.city.empty()&&a.postalCode.empty()&&a.country.empty())return;if(contact.addresses.size()>=4){truncated=true;return;}contact.addresses.push_back(std::move(a));};
            if(provider==EmailProvider::Google){
                contact.name=text(row["names"][0]["displayName"],256);contact.email=text(row["emailAddresses"][0]["value"],254);
                for(JsonObjectConst phone:row["phoneNumbers"].as<JsonArrayConst>()){auto value=text(phone["value"],96);
                    if(String(phone["type"]|"")=="mobile"&&!contact.mobile.length())contact.mobile=value;else if(!contact.phone.length())contact.phone=value;}
                auto address=row["addresses"][0];contact.street=text(address["streetAddress"],192);contact.city=text(address["city"],128);
                contact.postalCode=text(address["postalCode"],32);contact.country=text(address["country"],96);contact.notes=text(row["biographies"][0]["value"],1024);
                for(JsonObjectConst v:row["phoneNumbers"].as<JsonArrayConst>())addValue(contact.phones,label(v["type"]),v["value"],96);
                for(JsonObjectConst v:row["emailAddresses"].as<JsonArrayConst>())addValue(contact.emails,label(v["type"]),v["value"],254);
                for(JsonObjectConst a:row["addresses"].as<JsonArrayConst>())addAddress(a,label(a["type"]),true);
                contact.company=text(row["organizations"][0]["name"],192);contact.jobTitle=text(row["organizations"][0]["title"],128);
                auto date=row["birthdays"][0]["date"];const int year=date["year"]|0,month=date["month"]|0,day=date["day"]|0;
                if(!date.isNull()){if(year<0||year>2199||month<1||month>12||day<1||day>31)return fail("Ungültiges Kontakt-Geburtsdatum; Offline-Stand bleibt erhalten.");contact.birthday={uint16_t(year),uint8_t(month),uint8_t(day)};if(!ContactProtocol::validBirthday(contact.birthday))return fail("Ungültiges Kontakt-Geburtsdatum; Offline-Stand bleibt erhalten.");}
            }else{
                contact.name=text(row["displayName"],256);contact.email=text(row["emailAddresses"][0]["address"],254);
                contact.phone=text(row["businessPhones"][0],96);if(!contact.phone.length())contact.phone=text(row["homePhones"][0],96);contact.mobile=text(row["mobilePhone"],96);
                auto address=row["homeAddress"].as<JsonObjectConst>();if(!String(address["street"]|"").length())address=row["businessAddress"].as<JsonObjectConst>();
                contact.street=text(address["street"],192);contact.city=text(address["city"],128);contact.postalCode=text(address["postalCode"],32);contact.country=text(address["countryOrRegion"],96);contact.notes=text(row["personalNotes"],1024);
                for(JsonVariantConst v:row["businessPhones"].as<JsonArrayConst>())addValue(contact.phones,"Arbeit",v,96);
                for(JsonVariantConst v:row["homePhones"].as<JsonArrayConst>())addValue(contact.phones,"Privat",v,96);
                addValue(contact.phones,"Mobil",row["mobilePhone"],96);
                for(JsonObjectConst v:row["emailAddresses"].as<JsonArrayConst>())addValue(contact.emails,"Sonstige",v["address"],254);
                addAddress(row["homeAddress"],"Privat",false);addAddress(row["businessAddress"],"Arbeit",false);addAddress(row["otherAddress"],"Sonstige",false);
                contact.company=text(row["companyName"],192);contact.jobTitle=text(row["jobTitle"],128);
                const String birthday=text(row["birthday"],40);if(birthday.length()&&!ContactProtocol::parseBirthday(birthday.substring(0,10).c_str(),contact.birthday))return fail("Ungültiges Kontakt-Geburtsdatum; Offline-Stand bleibt erhalten.");
            }
            bool duplicate=false;for(const auto& old:contacts)if(old.id==contact.id)duplicate=true;if(!duplicate)contacts.push_back(std::move(contact));
        }
        const String next=provider==EmailProvider::Google?String(doc["nextPageToken"]|""):String(doc["@odata.nextLink"]|"");
        if(!next.length())break;if(contacts.size()>=128||page==3){truncated=true;break;}
        if(provider==EmailProvider::Google){if(next.length()>2048)return fail("Ungültiger Kontakt-Seitenschlüssel.");url=googleBase+"&"+form("pageToken",next);}
        else {if(next.length()>4096||!next.startsWith("https://graph.microsoft.com/v1.0/me/contacts?")||next.indexOf('@')>=0||next.indexOf('\\')>=0||next.indexOf('#')>=0)return fail("Fremde Kontakt-Folgeseite abgelehnt.");
            for(size_t i=0;i<next.length();++i)if(uint8_t(next[i])<=32||uint8_t(next[i])>=127)return fail("Ungültige Kontakt-Folgeseite.");url=next;}
    }
    SecurityWriteGuard security;Guard lock(mutex_);if(!current(job)||!phonebookEnabled_[job.slot]||!cloudEnabled_[job.slot]||!identity(info_[job.slot].email,email))return false;
    if(!PhonebookService::instance().saveFile(String("account-")+job.slot,true,contacts,truncated))return fail("Kontakte konnten nicht gespeichert werden; Offline-Stand bleibt erhalten.");
    auto& status=info_[job.slot];status.contactCount=contacts.size();status.contactsTruncated=truncated;status.contactsConnected=true;
    status.contactsMessage=truncated?"Kontakte synchronisiert; auf 128 Kontakte / Feldlängen begrenzt.":"Kontakte synchronisiert. Lokale Einträge bleiben getrennt erhalten.";
    if(permissionMissing_[job.slot]&&missingCapability_[job.slot]==AccountCapability::Contacts)permissionMissing_[job.slot]=false;
    return true;
}
}
