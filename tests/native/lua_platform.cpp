#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include "Arduino.h"
#include "src/apps/lua/LuaJobService.h"
#include "src/apps/lua/LuaEventFilter.h"
#include "src/apps/lua/LuaDeclarativeUi.h"
#include "src/apps/lua/LuaDataSubscriptions.h"
#include "esp_heap_caps.h"
#include "src/services/EnergyModel.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include "src/apps/lua/LuaJsonBridge.h"
size_t hp_test_livebytes=0;
uint32_t clockOffset=0;
uint32_t millis(){return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())+clockOffset;}
uint32_t micros(){return millis()*1000;}
enum class LuaRuntimeState {Opening,Foreground,Suspended,BackgroundExecuting};
struct EventLog {static EventLog& instance(){static EventLog log;return log;}void addTransient(const char*,const char*,const char*){}};
struct LuaApp {
    LuaRuntimeState lifecycle()const{return LuaRuntimeState::Foreground;}
    const char* id()const{return "fixture";}
    struct MemoryBudget {size_t used=0,peak=0;};
    static constexpr size_t kMemoryLimit=1024*1024;
    hp::LuaEventFilter filter;
    static LuaApp* self(lua_State*){static LuaApp app;return &app;}
    bool allows(hp::AppPermission p){return p==hp::AppPermission::Network||p==hp::AppPermission::UserStorage;}
    hp::LuaEventFilter& eventFilter(){return filter;}
    size_t memoryUsed()const{return 0;}size_t memoryPeak()const{return 0;}
    size_t nativeBytesUsed()const{return 0;}size_t nativeBytesPeak()const{return 0;}
    size_t uiObjectsUsed()const{return 0;}size_t uiObjectsPeak()const{return 0;}
    uint32_t budgetFailures()const{return 0;}
};
#include "validators.inc"
bool forceBusyLock=false;
namespace hp {
class SecurityService {public:static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked()const{return false;}};
class ConfigService {public:static ConfigService& instance(){static ConfigService s;return s;}uint32_t revision()const{return 1;}};
enum class MediaCommand:uint8_t { VolumeDown=7 };
class NetworkWorker {
public:
    using Fn=void(*)(void*);
    std::array<Fn,8> functions{};std::array<void*,8> contexts{};
    unsigned count=0,executed=0,localTransfers=0,networkTransfers=0;bool accept=true;
    static NetworkWorker& instance(){static NetworkWorker worker;return worker;}
    bool submit(Fn function,void* context,const char*){if(!accept||count>=8)return false;functions[count]=function;contexts[count++]=context;return true;}
    bool submitPriority(Fn function,void* context,const char* tag){return submit(function,context,tag);}
    bool submitLocal(Fn function,void* context,const char* tag,bool priority=false){return priority?submitPriority(function,context,tag):submit(function,context,tag);}
    bool submitTransfer(Fn function,void* context,const char* tag){++networkTransfers;return submit(function,context,tag);}
    bool submitLocalTransfer(Fn function,void* context,const char* tag){++localTransfers;return submit(function,context,tag);}
    void drain(){for(unsigned i=0;i<count;++i){functions[i](contexts[i]);++executed;}count=0;}
};
// Actual slot/ownership/cancellation/cleanup code. Only network completion is
// a test double, so this test does not claim to validate real sockets/TLS.
int discardedOwners=0;void discardLuaEvents(const char*){++discardedOwners;}
class AppFileStreamService {public:static bool validName(const String& name){return !name.empty();}};
#include "jobs.inc"
void LuaJobService::execute(void* context) {
    auto& slot=*static_cast<Slot*>(context);slot.busy=false;slot.finished=millis();
    if(slot.cancelled){slot.id=0;slot.request={};slot.result={};return;}
    slot.result.done=slot.result.ok=true;slot.result.status=200;
    if(slot.request.kind==5){assert(slot.request.handler);slot.request.handler(slot.request,slot.result);return;}
    slot.result.body="OK";
}
struct StorageService{static StorageService& instance(){static StorageService s;return s;}bool sdAvailable(){return true;}bool internalReady(){return true;}};
struct NetworkService{static NetworkService& instance(){static NetworkService s;return s;}bool online(){return false;}};
struct ImageResourceService{static constexpr size_t kBudget=8*1024*1024,kSlots=32;};
struct UiTheme{static UiTheme& instance(){static UiTheme s;return s;}uint32_t generation(){return 37;}};
struct IdleService{static IdleService& instance(){static IdleService s;return s;}uint8_t stage(){return 0;}};
struct LanguageService{static LanguageService& instance(){static LanguageService s;return s;}const char* active(){return "de-DE";}};
const char* tr(const char* key,const char* fallback){return !strcmp(key,"common.save")?"Speichern":fallback;}
uint32_t droppedLuaEvents(){return 2;}
}
#include "platform.inc"
// The host test compiles these functions from the production runtime, not a
// second sandbox/budget implementation.
#include "runtime.inc"
int slowNative(lua_State*){clockOffset+=101;return 0;}
void run(lua_State* L,const char* script) {
    if(luaL_loadstring(L,script)!=LUA_OK||protectedCall(L,0,0,100,1000)!=LUA_OK) {
        std::cerr<<lua_tostring(L,-1)<<"\n";assert(false);
    }
}
int main(int argc,char** argv) {
    using namespace hp;
    assert(nameValid("preferences")&&!nameValid("")&&!nameValid("other:key"));
    assert(fileNameValid("prefs.json")&&fileNameValid("sub/log.txt"));
    for(const char* path:{"","/system","../other","a/../b","a\\b","x:y","a//b","secret.tmp","secret.bak","a/b/c/d/file"})assert(!fileNameValid(path));
    assert(localAddress(0x0a000001)&&localAddress(0xc0a80101)&&localAddress(0xac100001));
    assert(!localAddress(0x7f000001)&&!localAddress(0xa9fe0001)&&!localAddress(0x08080808));
    auto& jobs=LuaJobService::instance();auto& worker=NetworkWorker::instance();
    LuaJobService::Request request;request.owner="app_a";request.permissions=perm(AppPermission::Network);
    uint32_t ids[4];for(auto& id:ids){id=jobs.submit(request);assert(id>0&&id<0x80000000);}
    assert(worker.executed==0&&jobs.submit(request)==0);
    LuaJobService::Result result;
    assert(jobs.result("app_a",ids[0],result)&&!result.done);
    assert(jobs.completionState("app_a",ids[0])==0);
    assert(!jobs.result("app_b",ids[0],result)&&!jobs.cancel("app_b",ids[0]));
    assert(jobs.cancel("app_a",ids[0]));assert(jobs.result("app_a",ids[0],result)&&result.cancelled);worker.drain();
    assert(!jobs.result("app_a",ids[0],result));
    assert(jobs.result("app_a",ids[1],result)&&result.done&&result.body=="OK");
    assert(!jobs.release("app_b",ids[1]));assert(jobs.release("app_a",ids[2]));assert(!jobs.result("app_a",ids[2],result));
    assert(jobs.completionState("app_a",ids[1])==1&&jobs.completionState("app_b",ids[1])==0);
    jobs.cancelOwner("app_a");
    assert(discardedOwners>0&&jobs.completionState("app_a",ids[1])==0);
    worker.accept=false;assert(jobs.submit(request)==0);worker.accept=true;
    auto id=jobs.submit(request);assert(id);forceBusyLock=true;
    jobs.cancelOwner("app_a");assert(jobs.submit(request)==0);forceBusyLock=false;
    worker.drain();assert(!jobs.result("app_a",id,result));
    id=jobs.submit(request);worker.drain();clockOffset+=61001;jobs.cleanup();
    assert(!jobs.result("app_a",id,result));
    for(unsigned i=0;i<1000;++i){id=jobs.submit(request);assert(id);worker.drain();assert(jobs.cancel("app_a",id));}
    {
      LuaJobService::Request file;file.owner="files";file.kind=5;
      file.required=perm(AppPermission::UserStorage);file.permissions=file.required;
      file.handler=[](const LuaJobService::Request&,LuaJobService::Result&){};
      const auto local=worker.localTransfers,net=worker.networkTransfers;
      file.lane=LuaJobService::Request::Lane::LocalTransfer;
      id=jobs.submit(file);assert(id&&worker.localTransfers==local+1&&worker.networkTransfers==net);
      worker.drain();assert(jobs.release("files",id));
      file.lane=LuaJobService::Request::Lane::Transfer;
      id=jobs.submit(file);assert(id&&worker.networkTransfers==net+1);
      worker.drain();assert(jobs.release("files",id));
      file.kind=0;assert(jobs.submit(file)==0); // Lua HTTP cannot select a native lane.
      file.owner="@users-native";file.kind=5;file.lane=LuaJobService::Request::Lane::LocalTransfer;
      file.permissions=file.required=perm(AppPermission::CalendarRead);static unsigned profileCalls=0;
      file.handler=[](const LuaJobService::Request&,LuaJobService::Result& r){++profileCalls;r.body="profiles";};
      id=jobs.submit(file);assert(id);worker.drain();assert(jobs.result(file.owner.c_str(),id,result)&&result.body=="profiles"&&profileCalls==1);assert(jobs.release(file.owner.c_str(),id));
      file.kind=1;assert(!jobs.submit(file)); // Storage jobs cannot dispatch calendar handlers.
    }
    EnergySnapshot empty;assert(!empty.gridPower.available());assert(std::isnan(empty.pvPower.value));
    EnergyValue selected,low,high;low.value=-123;low.quality=EnergyQuality::Measured;
    high.value=456;high.quality=EnergyQuality::Measured;int priority=INT32_MIN;
    chooseEnergyValue(selected,low,10,priority);assert(selected.value==-123);
    chooseEnergyValue(selected,high,20,priority);assert(selected.value==456&&priority==20);
    chooseEnergyValue(selected,low,5,priority);assert(selected.value==456);
    high.quality=EnergyQuality::Unavailable;chooseEnergyValue(selected,high,100,priority);assert(selected.value==456);
    low.quality=EnergyQuality::Calculated;chooseEnergyValue(selected,low,100,priority);assert(selected.value==456);
    LuaApp::MemoryBudget budget;
    void* allocation=luaAppAlloc(&budget,nullptr,123,32);assert(allocation&&budget.used==32);
    memset(allocation,0x42,32);
    void* rejected=luaAppAlloc(&budget,allocation,32,LuaApp::kMemoryLimit+1);assert(!rejected&&budget.used==32&&static_cast<unsigned char*>(allocation)[0]==0x42);
    void* bigger=luaAppAlloc(&budget,allocation,32,64);assert(bigger&&budget.used==64&&static_cast<unsigned char*>(bigger)[0]==0x42);
    luaAppAlloc(&budget,bigger,64,0);assert(budget.used==0);
    auto* L=lua_newstate(luaAppAlloc,&budget,0);assert(L);openAppLibraries(L);registerLuaJsonBridge(L);
    std::string nativeList="[";for(unsigned i=0;i<600;++i){if(i)nativeList+=',';nativeList+="1";}nativeList+=']';
    assert(pushLuaJsonBounded(L,nativeList.c_str(),nativeList.size(),2048)==1&&lua_istable(L,-1)&&lua_rawlen(L,-1)==600);lua_pop(L,1);
    assert(pushLuaJson(L,nativeList.c_str(),nativeList.size())==2&&lua_isnil(L,-2));lua_pop(L,2);
    assert(pushLuaJsonBounded(L,nativeList.c_str(),nativeList.size(),2049)==1&&lua_isnil(L,-1));lua_pop(L,1);
    registerLuaPlatformBridge(L);
    {
        const char* cached="{\"online\":true,\"nozzle\":{\"current\":205},\"axes\":[{\"homed\":true}]}";
        assert(pushLuaJson(L,cached,strlen(cached))==1&&lua_istable(L,-1));
        String encoded;assert(encodeLuaJson(L,-1,encoded)&&encoded.length()>0);lua_pop(L,1);
        const int top=lua_gettop(L);assert(pushLuaJson(L,"{",1)==2&&lua_isnil(L,-2));lua_settop(L,top);
    }
    run(L,R"lua(
        local caps=hp_capabilities()
        assert(caps.apis.language==1 and hp_language()=='de-DE')
        assert(hp_tr('common.save')=='Speichern' and hp_tr('missing','Deutsch')=='Deutsch')
        assert(not pcall(hp_tr,string.rep('x',97)))
        assert(caps.version==1 and caps.apis.jobs==1 and caps.apis.http_stream==1)
        assert(not caps.apis.websocket and not caps.apis.mqtt and caps.apis.printer==1)
        assert(caps.permissions['storage.writeAppData'] and not caps.permissions['storage.nasRead'])
        assert(caps.limits.stream_chunk_bytes==8192 and caps.limits.jobs==4)
        assert(hp_system_snapshot().theme_generation==37 and hp_events_status().dropped==2)
        assert(hp_events_subscribe({'theme.changed','theme.changed'}))
        assert(hp_events_status().subscriptions==1 and hp_events_status().filtered)
        assert(not pcall(hp_events_subscribe,{'bad/name'}))
        assert(hp_events_status().subscriptions==1)
        assert(hp_events_subscribe({}) and hp_events_status().subscriptions==0)
        assert(hp_events_subscribe(nil) and not hp_events_status().filtered)
    )lua");
    run(L,"assert(io==nil and os==nil and debug==nil and package==nil and require==nil and dofile==nil and loadfile==nil)");
    run(L,R"lua(
        local a=assert(hp_json_decode('{"n":-12,"b":true,"s":"hello","nested":{"x":7},"array":[1,2,3]}'))
        assert(a.n==-12 and a.b and a.s=='hello' and a.nested.x==7 and a.array[3]==3)
        local encoded=assert(hp_json_encode(a));local b=assert(hp_json_decode(encoded))
        assert(b.n==-12 and b.array[2]==2,encoded)
        local precise=assert(hp_json_decode('{"epoch":1791540001,"revision":16777217}'))
        assert(math.type(precise.epoch)=='integer' and precise.epoch==1791540001)
        assert(hp_json_encode(precise):find('1791540001',1,true))
        local nulls=assert(hp_json_decode('[1,null,3]'));assert(nulls[2]==hp_json_null)
        assert(hp_json_encode(nulls)=='[1,null,3]')
        assert(hp_json_decode('{')==nil)
        assert(hp_json_decode(string.rep('x',32769))==nil)
        assert(hp_json_decode(string.rep('[',13)..'0'..string.rep(']',13))==nil)
        local cycle={};cycle.x=cycle;assert(hp_json_encode(cycle)==nil)
        assert(hp_json_encode({[1]=1, named=2})==nil)
        assert(hp_json_encode({x=string.rep('x',8193)})==nil)
        local many={};for i=1,513 do many[i]=i end;assert(hp_json_encode(many)==nil)
        local forecast={time={},temperature={},rain={},code={}}
        for i=1,168 do forecast.time[i]=i;forecast.temperature[i]=12;forecast.rain[i]=i%100;forecast.code[i]=3 end
        assert(hp_json_encode(forecast)==nil,'default element budget must remain 512')
        local full=assert(hp_json_encode(forecast,{max_elements=1024}))
        assert(hp_json_decode(full)==nil)
        local restored=assert(hp_json_decode(full,{max_elements=1024}))
        assert(#restored.time==168 and restored.rain[168]==68)
        assert(hp_json_encode(forecast,{max_elements=600})==nil)
        for _,limit in ipairs({0,-1,2049,1.5,'bad'}) do
            assert(not pcall(hp_json_decode,full,{max_elements=limit}))
            assert(not pcall(hp_json_encode,forecast,{max_elements=limit}))
        end
        assert(hp_json_decode(string.rep('x',32769),{max_elements=2048})==nil)
        shared_main_value=99
    )lua");
    assert(luaL_loadstring(L,"while true do end")==LUA_OK);
    assert(protectedCall(L,0,0,100,3)!=LUA_OK);lua_pop(L,1);
    lua_pushcfunction(L,slowNative);assert(protectedCall(L,0,0,30,250)!=LUA_OK);lua_pop(L,1);
    lua_close(L);assert(budget.used==0&&hp_test_livebytes==0);
    // settings.lua gets a fresh state rather than main's global environment.
    L=lua_newstate(luaAppAlloc,&budget,0);assert(L);openAppLibraries(L);run(L,"assert(shared_main_value==nil)");
    assert(luaL_loadstring(L,"x=string.rep('z',2097152)")==LUA_OK);
    assert(protectedCall(L,0,0,100,1000)!=LUA_OK);lua_pop(L,1);
    lua_close(L);assert(budget.used==0&&hp_test_livebytes==0);
    assert(argc==2);
    for(const char* entry:{"main.lua","settings.lua"}){
        if(!std::ifstream(std::string(argv[1])+"/examples/lua-app-template/"+entry).good())continue;
        L=lua_newstate(luaAppAlloc,&budget,0);assert(L);openAppLibraries(L);
        run(L,R"lua(
          function hp_root() return 1 end
          function hp_label(_,s) return 2 end
          function hp_button(_,s) return 3 end
          function hp_align(...) end
          function hp_on_click(...) end
          function hp_set_text(...) end
          function hp_capabilities() return {apis={jobs=1}} end
          function hp_events_subscribe(...) return true end
          function hp_events_status() return {dropped=0} end
          function hp_system_snapshot() return {} end
          function hp_millis() return 1001 end
          function hp_kv_set_string(...) return 42 end
          function hp_kv_set_bool(...) return 42 end
          function hp_job_result(id) assert(id==42);return {done=true,ok=true} end
          function hp_job_release(id) assert(id==42);released=true;return true end
          function hp_job_cancel(id) assert(id==42);return true end
          function hp_open_app_settings() navigation='settings' end
          function hp_open_app() navigation='main' end
        )lua");
        std::string path=std::string(argv[1])+"/examples/lua-app-template/"+entry;
        assert(luaL_loadfile(L,path.c_str())==LUA_OK);assert(protectedCall(L,0,0,100,1000)==LUA_OK);
        if(!strcmp(entry,"main.lua"))run(L,"on_open();save_example();on_suspend();on_resume();save_example();on_event('storage.changed',{job=42});assert(released);open_settings();assert(navigation=='settings');on_tick();on_close()");
        else run(L,"on_open();save_settings();on_tick();assert(released);return_to_app();assert(navigation=='main');on_close()");
        lua_close(L);assert(budget.used==0&&hp_test_livebytes==0);
    }
    #include "lua_vnext.inc"
    std::cout<<"PASS actual Lua sandbox, V3 incremental UI/budgets, jobs, JSON integer precision, isolation and available optional local packages\n";
}
