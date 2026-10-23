#pragma once
#include "printer/RepRapPrinterAdapter.h"
#include "printer/MoonrakerPrinterAdapter.h"
#include "printer/OctoPrintPrinterAdapter.h"
#include "printer/PrinterComponents.h"
#include "printer/PrinterActionCatalog.h"
#include "printer/PrinterRuntimeModel.h"
#include <freertos/semphr.h>
#include <atomic>
namespace hp {
class PrinterService {
public:
    static constexpr size_t kOwners=2,kProfiles=5;
    static PrinterService& instance();
    bool begin();
    bool arm(const char* owner,AppPermissions permissions);
    String profiles(const char* owner,String* error=nullptr,bool refresh=false);
    String profileState(const char* owner);
    // Cache-only/zero-wait UI reads; requests are consumed by NetworkWorker.
    String snapshot(const char* owner,const char* id,bool foreground=true);
    bool snapshotRevision(const char* owner,const char* id,uint32_t& revision,uint32_t& timestamp,bool& pending,bool& online);
    bool watch(const char* owner,const char* id,bool enabled);
    // Web diagnostics: no arming, reload request, foreground marking or HTTP.
    String cachedSnapshot(const PrinterProfile& expected);
    String files(const char* owner,const char* id,const String& directory,bool macros=false,bool refresh=false,uint16_t offset=0);
    String console(const char* owner,const char* id);
    String history(const char* owner,const char* id,const char* sensor,size_t offset,size_t count);
    String operation(const char* owner,const char* id);
    uint32_t command(const char* owner,AppPermissions,const char* id,const String& kind,const String& parameters);
    void cleanupOwner(const char* owner,bool suspending);
    static const char* diagnosis(const String&);
private:
    struct Entry {
        PrinterProfile profile;RepRapPrinterAdapter adapter;MoonrakerPrinterAdapter moonraker;OctoPrintPrinterAdapter octoprint;PrinterStatusPtr status;
        PrinterOperation operation;std::shared_ptr<PrinterTemperatureHistory> history;
        bool isMoonraker()const{return profile.protocol=="moonraker"||profile.protocol=="klipper";}
        bool isOctoPrint()const{return profile.protocol=="octoprint"||profile.protocol=="marlin_octoprint";}
        PrinterAdapter& selectedAdapter(){return isMoonraker()?static_cast<PrinterAdapter&>(moonraker):
            isOctoPrint()?static_cast<PrinterAdapter&>(octoprint):static_cast<PrinterAdapter&>(adapter);}
        const char* selectedApi()const{return isMoonraker()?moonraker.api():isOctoPrint()?octoprint.api():adapter.api();}
        String takeReply(){return isMoonraker()?moonraker.takeReply():isOctoPrint()?octoprint.takeReply():adapter.takeReply();}
        String snapshot,files,macros,console,lastCommandError,connectionError,filesDirectory="0:/gcodes",macrosDirectory="0:/macros";
        uint32_t due=0,lastSeen=0,revision=0,lastUpdated=0,contentHash=0,pendingUntil=0,httpRequests=0,lastPollRequests=0,lastPollMs=0;
        uint32_t consoleSeen=0,consolePolled=0;
        unsigned failures=0;bool wantFiles=false,wantMacros=false,observed=false;uint16_t filesOffset=0,macrosOffset=0;
    };
    struct Owner {
        char id[32]{};std::atomic<bool> active{false};
        bool busy=false,sync=true;uint32_t generation=0,syncedAt=0;std::atomic<uint32_t> closedAt{0};
        AppPermissions permissions=0;String profiles,error;
        Entry entries[kProfiles];size_t count=0;
    };
    Owner owners_[kOwners];SemaphoreHandle_t lock_=nullptr;size_t cursor_=0;
    uint32_t nextNetwork_=0;
    Owner* findOwner(const char*);
    Entry* findEntry(Owner*,const char*);
    void tick();
    void loadProfiles(Owner&);
    static void maintenance(void*);
    static void executeCommand(const LuaJobService::Request&,LuaJobService::Result&);
    void runCommand(const LuaJobService::Request&,LuaJobService::Result&);
    String makeSnapshot(const Entry&,const String& error,uint32_t* digest=nullptr)const;
};
}
