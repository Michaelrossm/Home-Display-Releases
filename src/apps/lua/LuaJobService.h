#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../core/AppPermissions.h"
#include "LuaOwnerCleanup.h"
namespace hp {
class LuaJobService {
public:
    static constexpr size_t kSlots=4, kResponseBytes=16384, kBodyBytes=8192;
    struct Header { String name, value; };
    struct Result { bool done=false,ok=false,cancelled=false; int status=0; size_t size=0; String body,error;
        uint64_t processed=0,total=0;uint32_t queuedAt=0,startedAt=0,finishedAt=0;
        bool retryable=false,coalesced=false; };
    struct Request;
    using Handler=void(*)(const Request&,Result&);
    using Progress=void(*)(void*,uint64_t,uint64_t);
    using StreamRead=size_t(*)(void*,uint64_t,uint8_t*,size_t);
    using StreamWrite=bool(*)(void*,const uint8_t*,size_t,String&);
    struct Request {
        enum class Lane:uint8_t {Automatic,LocalTransfer,Transfer};
        Lane lane=Lane::Automatic; // Firmware-owned dispatch hint, never Lua input.
        String owner, url, body, contentType, bearerSecret, basicSecret, context;
        Header headers[8]; size_t headerCount=0;
        uint32_t timeout=1500; uint8_t method=0;
        bool coalesce=false; // Explicitly declared side-effect-free GET; same owner/credentials only.
        size_t responseLimit=kResponseBytes; // Trusted native HTTP only; bounded to 64 KiB.
        uint32_t resource=0,offset=0,length=8192,deadlineMs=60000;
        size_t expected=0;String fileName;uint8_t streamMode=0;
        uint8_t kind=0; // HTTP or sandboxed storage, same queue/cache.
        AppPermissions permissions=0;
        Handler handler=nullptr;
        AppPermissions required=0;
        const std::atomic<bool>* cancelled=nullptr; // Native handler only; slot remains alive while busy.
        const char* completion=nullptr; // Firmware-owned static event name.
        Progress progress=nullptr;void* progressContext=nullptr;
        StreamRead nativeRead=nullptr;void* nativeReadContext=nullptr; // Trusted worker-local stream, never Lua pointers.
        StreamWrite nativeWrite=nullptr;void* nativeWriteContext=nullptr; // Trusted bounded worker consumer, never Lua pointers.
        void(*discardResult)(const char*,const String&)=nullptr; // Release unclaimed native resources on cancel/expiry.
    };
    static LuaJobService& instance();
    static void executeHttp(const Request&,Result&); // Existing NetworkWorker only, no nested job/wait.
    static bool resolveHttpTarget(const Request&,String& pinned,String& hostname); // Worker-only shared permission/DNS policy.
    uint32_t submit(const Request& request);
    bool result(const char* owner,uint32_t id,Result& out);
    // -1: lock busy (retry), 0: stale/cancelled, 1: matching completed owner.
    int completionState(const char* owner,uint32_t id);
    bool cancel(const char* owner,uint32_t id);
    bool release(const char* owner,uint32_t id); // Completed results only, no network mutation.
    struct Diagnostics{uint32_t completed,cancelled,timeouts,retryable,coalesced;};
    Diagnostics diagnostics()const{return {completed_.load(),cancelled_.load(),timeouts_.load(),retryable_.load(),coalesced_.load()};}
    void cancelOwner(const char* owner,bool suspending=false);
    bool ownerSuspending()const{return ownerSuspending_;}
    void cleanup(); // Zero-wait UI maintenance, no new timer/task.
    using OwnerCleanup=LuaOwnerCleanup::Handler;
    bool registerOwnerCleanup(OwnerCleanup handler){return ownerCleanup_.add(handler);}
private:
    struct Slot {
        char owner[32]{}; // Written only by guarded UI submit; stable for worker.
        Request request; Result result; uint32_t id=0,finished=0;
        bool busy=false,sharedReady=false; uint32_t credentialRevision=0; std::atomic<bool> cancelled{false};
    };
    Slot slots_[kSlots];
    SemaphoreHandle_t lock_=nullptr;
    uint32_t sequence_=0;
    LuaOwnerCleanup ownerCleanup_;
    bool ownerSuspending_=false; // UI-only, scoped to native cleanup callbacks.
    std::atomic<uint32_t> completed_{0},cancelled_{0},timeouts_{0},retryable_{0},coalesced_{0};
    static bool sameRead(const Request&,const Request&);
    static void progress(void*,uint64_t,uint64_t);
    static void execute(void* context);
    void run(Slot& slot);
};
}
