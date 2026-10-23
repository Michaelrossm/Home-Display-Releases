#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "EmailAccountService.h"
#include "AccountAuthTypes.h"
#include "CloudAccountTypes.h"
#include "../apps/calendar/CalendarTypes.h"
#include "../apps/calendar/EventDetails.h"
namespace hp {
enum class CalendarAccountStatus : uint8_t { Ok, AuthRequired, NetworkError, SyncError, Disabled };
enum class CalendarSyncState : uint8_t { Idle, Offline, Pending, Syncing, Error };
struct CalendarConnectionInfo {
    String message = "Nicht verbunden", authorizationUrl, userCode;
    String email, name;
    EmailProvider provider = EmailProvider::Google;
    OAuthPurpose purpose = OAuthPurpose::Calendar;
    AccountAuthState authState = AccountAuthState::Idle;
    uint8_t slot = 255;
    uint32_t expiresIn = 0;
    bool configured = false;
    bool connected = false, working = false, truncated = false;
    uint8_t eventCount = 0;
    uint16_t contactCount=0;
    bool contactsTruncated=false,contactsConnected=false,davCalendarConnected=false;
    String contactsMessage="Telefonbuch-Synchronisierung ausgeschaltet.";
    uint32_t lastSyncMs = 0;
};
struct CalendarOAuthSetup { String clientId, redirect, message; bool secretStored = false, own = false, standardAvailable = false, busy = false; };
// Provider HTTPS and calendar/token IO run on the existing NetworkWorker.
// Native provider configuration writes use central storage under the PIN guard.
class CalendarSyncService {
public:
    static CalendarSyncService& instance();
    uint32_t dataRevision()const{return dataRevision_.load();}
    bool requestRange(int64_t from,int64_t to); // Queue only; cached reads never wait for cloud.
    bool begin();
    void backgroundTick();
    CalendarSyncState state() const { return state_.load(); }
    CalendarConnectionInfo info(uint8_t slot);
    CalendarOAuthSetup setup(EmailProvider provider,OAuthPurpose purpose=OAuthPurpose::Calendar);
    bool configure(EmailProvider provider, const String& clientId, const String& redirect, const char* secret,OAuthPurpose purpose=OAuthPurpose::Calendar);
    bool configurePublic(EmailProvider,OAuthPurpose,bool own,const String& clientId,const String& redirect);
    bool startMicrosoftLogin();
    bool startGoogleLogin(OAuthPurpose purpose=OAuthPurpose::Identity);
    bool cancelLogin(EmailProvider);
    bool disconnectAccount(EmailProvider);
    void pollLogin(){backgroundTick();}
    CalendarConnectionInfo getAccountState(EmailProvider);
    CalendarConnectionInfo getDisplayInstructions(EmailProvider provider){return getAccountState(provider);}
    void accountChanged(uint8_t);
    bool connect(uint8_t slot);
    bool sync(uint8_t slot);
    bool disconnect(uint8_t slot);
    void invalidate(uint8_t slot); // Called after account replacement/deletion.
    bool completeGoogle(const String& state, const String& code);
    size_t copyEvents(uint8_t slot, EventRecord* output, size_t capacity);
    bool copyEventDetails(uint8_t slot,uint64_t localId,EventDetails& output);
    std::vector<AccountInfo> accounts();
    std::vector<AccountInfo> accountsWithCapability(AccountCapability);
    bool isConnected(const String& accountId);
    AccountCapabilities capabilities(const String& accountId);
    AccountInfo accountInfo(const String& accountId);
    CloudAccountState requestCapability(const String& accountId,AccountCapability);
    bool authorizeCapability(const String& accountId,AccountCapability);
    bool disconnectAccount(const String& accountId);
    bool removeAccount(const String& accountId);
    bool setAccountEnabled(const String& accountId,bool enabled);
    bool setPhonebookEnabled(const String& accountId,bool enabled);
    CloudCalendarClient calendarClient(const String& accountId){return CloudCalendarClient(accountId);}
    std::vector<CloudCalendarCollection> calendarCollections(const String& accountId);
    bool configureCalDav(uint8_t slot,AccountProvider,const String& server,const String& user,const char* password);
    bool requestCalDavConfiguration(uint8_t slot,AccountProvider,const String& server,const String& user,const char* password="");
    bool testCalDav(uint8_t slot);
    String calDavServer(uint8_t slot);
private:
    void calendarDataChanged();
    enum class Operation : uint8_t { Restore, Connect, Poll, Callback, Sync, Disconnect };
    struct Job { uint8_t slot; Operation operation; uint32_t generation; String code; EmailProvider provider; OAuthPurpose purpose; bool identify; int64_t rangeFrom=0,rangeTo=0; };
    struct ConfigJob {EmailProvider provider;OAuthPurpose purpose;bool own;String clientId,redirect;};
    struct DavMetadata {uint8_t version=1,provider=uint8_t(AccountProvider::CalDav),enabled=1;char server[513]{},user[255]{},displayName[97]{};};
    struct DavSecret {uint8_t version=1,provider=0;char server[513]{},user[255]{},password[193]{};};
    struct DavConfigJob {uint8_t slot;uint32_t generation;AccountProvider provider;String server,user;char password[193]{};~DavConfigJob(){volatile char* p=password;for(size_t i=0;i<sizeof(password);++i)p[i]=0;}};
    struct EnableJob {uint8_t slot;uint32_t generation;bool enabled;bool phonebook=false;};
    struct Session {
        String state, verifier, deviceCode, email;
        EmailProvider provider = EmailProvider::Google;
        OAuthPurpose purpose = OAuthPurpose::Calendar;
        uint32_t expires = 0, next = 0, interval = 5000;
        bool consumed = false;
    };
    struct ProviderRecord { uint8_t version = 1; char clientId[193]{}, redirect[255]{}, secret[257]{}; };
    struct TokenRecord { uint8_t version = 1, provider = 0; char email[255]{}, clientId[193]{}, refresh[4097]{}; };
    static void run(void*);
    bool enqueue(uint8_t, Operation, const String& code = "");
    void execute(Job&);
    bool providerRecord(EmailProvider, ProviderRecord&,OAuthPurpose purpose=OAuthPurpose::Calendar);
    bool configureRecord(EmailProvider,const String&,const String&,const char*,OAuthPurpose,bool own,bool requireSecret);
    static void runConfig(void*);
    void loadSetups();
    bool startLogin(EmailProvider,OAuthPurpose,uint8_t target=255);
    uint8_t selectSlot(EmailProvider,bool freeAllowed);
    CalendarOAuthSetup& setupRef(EmailProvider,OAuthPurpose);
    void finishLogin(const Job&,AccountAuthState,const char*);
    bool tokenRecord(uint8_t, TokenRecord&);
    bool current(const Job&);
    void publish(const Job&, const char* message, bool connected = false);
    bool acceptTokens(Job&, EmailProvider, const String& email, const char* clientId, String& access, String& refresh);
    bool fetchCalendar(Job&, EmailProvider, const String& email, const String& access);
    bool discoverOAuthCalendars(Job&,EmailProvider,const String& access);
    std::vector<CloudCalendarCollection> selectedOAuthCalendars(Job&,EmailProvider,const String& email);
    bool restoreCalendarCache(Job&);
    bool commitCalendarCache(Job&,EventRecord*,EventDetailsSnapshot*,uint8_t count,bool truncated);
    bool fetchContacts(Job&,EmailProvider,const String& email,const String& access);
    void clearSession(uint8_t);
    void clearAccess(uint8_t);
    bool loadDav(uint8_t);
    bool executeDav(Job&);
    static void runDavConfig(void*);
    static void runEnable(void*);
    AccountInfo cloudInfo(uint8_t);
    void publishAccountEvents();
    bool persistCloudMetadata(uint8_t);
    static int accountSlot(const String&);
    friend class CloudCalendarClient;
    SemaphoreHandle_t mutex_ = nullptr;
    CalendarConnectionInfo info_[5];
    Session sessions_[5];
    EventRecord* events_[5]{};
    EventDetailsSnapshot* details_[5]{};
    bool phonebookEnabled_[5]{}; // Opt-in; absent/invalid metadata always means off.
    char* access_[5]{}; // Private, volatile PSRAM cache; never exposed to apps.
    uint32_t accessUntil_[5]{};
    uint32_t generation_[5]{}, nextSync_[5]{};
    bool pending_[5]{}, restored_[5]{};
    bool loaded_[5]{}, identify_[5]{}, deleting_[5]{};
    uint32_t deleteRetry_[5]{};
    uint8_t lastLoginSlot_[2]{255,255};
    CalendarConnectionInfo providerStatus_[2];
    OAuthPurpose previousPurpose_[5]{};
    DavMetadata dav_[5];
    bool davConfigured_[5]{},davConfigPending_[5]{},cloudEnabled_[5]{true,true,true,true,true};
    CloudAccountState cloudStatus_[5]{CloudAccountState::Disconnected,CloudAccountState::Disconnected,CloudAccountState::Disconnected,CloudAccountState::Disconnected,CloudAccountState::Disconnected};
    bool permissionMissing_[5]{};
    AccountCapability missingCapability_[5]{};
    bool forgetAccount_[5]{};
    std::vector<CloudCalendarCollection> collections_[5];
    CloudAccountState lastPublishedState_[5]{};
    AccountCapabilities lastPublishedCaps_[5]{};
    bool lastPublishedConnected_[5]{};
    uint32_t lastTick_ = 0;
    CalendarOAuthSetup google_, googleDevice_, microsoft_;
    std::atomic<CalendarSyncState> state_{CalendarSyncState::Idle};
    std::atomic<uint32_t> dataRevision_{1};
    int64_t rangeFrom_[5]{},rangeTo_[5]{};
};
}
