#include "LuaBackgroundRun.h"
#include "LuaEvents.h"
#include "LuaEventFilter.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "../../../src/core/AppLifecycleCore.h"
using TaskHandle_t=int;
int currentTask=1;TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
uint32_t now=0,clockStep=0;uint32_t micros(){return now+=clockStep;}
uint32_t millis(){return now/1000;}
int mutations=0,jobs=0;std::string owner;
namespace hp {
enum class AppKind{Lua,Native};
struct IApp{virtual AppKind kind()const{return AppKind::Lua;}virtual const char* id()const{return "test-owner";}virtual ~IApp()=default;};
struct EventLog{static EventLog& instance(){static EventLog e;return e;}void addTransient(const char*,const char*,const char*){}};
class LuaApp:public IApp{public:
 static TaskHandle_t backgroundUiTask_;bool faulted_=false,settingsState_=false,paused_=true;
 lua_State* L_=nullptr;LuaRuntimeState lifecycle_=LuaRuntimeState::Suspended;
 LuaEventFilter eventFilter_;
 const char* id()const override{return "test-owner";}
 static bool dispatchEvents(bool allowBackground=true);void quarantine(const char*){assert(false);}
 bool runBackground(const char* event=nullptr,uint32_t job=0,uint32_t* elapsedUs=nullptr);
};
struct AppManager{IApp* background=nullptr;IApp* visible=nullptr;bool enabled=true;
 static AppManager& instance(){static AppManager m;return m;}IApp* current(){return visible;}
 IApp* find(const char* id){return background&&!strcmp(id,background->id())?background:nullptr;}bool isEnabled(IApp*){return enabled;}};
struct AppBackgroundScheduler{bool permitted=true,registered=true;static AppBackgroundScheduler& instance(){static AppBackgroundScheduler s;return s;}
 bool hasPolicy(const char*){return registered;}bool ownerAllowed(const char*){return permitted;}void recordLuaEvent(uint32_t){}};
struct LuaJobService{static LuaJobService& instance(){static LuaJobService j;return j;}int completionState(const char* id,uint32_t job){return job==123&&!strcmp(id,"test-owner");}};
constexpr uint32_t kTickHardBudgetMs=12;constexpr int kTickHookCalls=60;
int protectedCall(lua_State* L,int nargs,int nresults,uint32_t,int){return lua_pcall(L,nargs,nresults,0);}
TaskHandle_t LuaApp::backgroundUiTask_=1;
#include "background_method.inc"
#include "event_dispatch.inc"
}
hp::LuaApp* app;
int ui(lua_State*){++mutations;return 0;}
int http(lua_State* L){assert(currentTask==1&&app->lifecycle_==hp::LuaRuntimeState::BackgroundExecuting);owner=app->id();++jobs;lua_pushinteger(L,123);return 1;}
int slow(lua_State*){now+=10000;return 0;}
void script(lua_State* L,const char* code){assert(luaL_dostring(L,code)==LUA_OK);}
int integer(lua_State* L,const char* name){lua_getglobal(L,name);int n=lua_tointeger(L,-1);lua_pop(L,1);return n;}
int main(){hp::LuaApp runtime;app=&runtime;auto* L=luaL_newstate();runtime.L_=L;luaL_openlibs(L);
 lua_register(L,"hp_label",ui);lua_register(L,"hp_http_get",http);lua_register(L,"hp_json_encode",slow);
 hp::protectLuaBackgroundCalls(L);
 script(L,"local co=coroutine.create(function() local ok,value=pcall(function() coroutine.yield('wait');return 9 end);assert(ok and value==9) end);local ok,value=coroutine.resume(co);assert(ok and value=='wait');assert(coroutine.resume(co));assert(string.gsub('abc','b','x')=='axc')");
 assert(runtime.runBackground());assert(runtime.lifecycle_==hp::LuaRuntimeState::Suspended&&runtime.paused_);
 script(L,"count=0; function on_background() count=count+1; request=hp_http_get({url='https://example.com'}) end");
 assert(runtime.runBackground());assert(integer(L,"count")==1&&integer(L,"request")==123&&jobs==1&&owner=="test-owner");
 currentTask=2;assert(!runtime.runBackground());assert(integer(L,"count")==1);currentTask=1;
 script(L,"local cached=hp_label; function on_background() cached(nil,'forbidden') end");assert(!runtime.runBackground());assert(!mutations&&!runtime.faulted_&&runtime.lifecycle_==hp::LuaRuntimeState::Suspended);
 script(L,"function on_background() local ok=pcall(hp_label); assert(not ok) end");assert(runtime.runBackground());assert(!mutations);
 script(L,"function on_background() while true do end end");assert(!runtime.runBackground());
 script(L,"local p=pcall; function on_background() while true do p(function() while true do end end) end end");assert(!runtime.runBackground());
 script(L,"function on_background() xpcall(function() while true do end end,function() while true do end end) end");assert(!runtime.runBackground());
 script(L,"local co=coroutine.wrap(function() while true do end end); function on_background() co() end");assert(!runtime.runBackground());
 script(L,"local it=string.gmatch('abc','(.)'); function on_background() it() end");assert(!runtime.runBackground());
 script(L,"function on_background() string.rep('x',1000000) end");assert(!runtime.runBackground());
 script(L,"function on_background() local x <close> = setmetatable({},{__close=function() while true do end end}); error('close') end");assert(!runtime.runBackground());
 script(L,"function on_background() hp_json_encode({}) end");assert(!runtime.runBackground());
 now=0;clockStep=1000;script(L,"function on_background() while true do end end");assert(!runtime.runBackground());clockStep=0;
 script(L,"function on_background() error('local failure') end");assert(!runtime.runBackground());assert(!runtime.faulted_&&runtime.paused_);
 script(L,"function on_event(name,event) assert(name=='http.completed' and event.job==123); count=count+1 end");
 hp::AppManager::instance().background=&runtime;
 assert(hp::publishLuaEvent("test-owner","http.completed",123));hp::LuaApp::dispatchEvents();assert(integer(L,"count")==2&&runtime.lifecycle_==hp::LuaRuntimeState::Suspended);
 hp::AppBackgroundScheduler::instance().permitted=false;assert(hp::publishLuaEvent("test-owner","http.completed",123));hp::LuaApp::dispatchEvents();assert(integer(L,"count")==2);
 now+=500000;hp::LuaApp::dispatchEvents();assert(integer(L,"count")==2);now+=500000;
 hp::AppBackgroundScheduler::instance().permitted=true;hp::LuaApp::dispatchEvents();assert(integer(L,"count")==3);
 assert(hp::publishLuaEvent("test-owner","http.completed",123));assert(hp::publishLuaEvent("other-owner","http.completed",123));hp::discardLuaEvents("test-owner");
 hp::LuaEvent event{};assert(hp::receiveLuaEvent(event)&&!strcmp(event.owner,"other-owner"));assert(!hp::receiveLuaEvent(event));
 script(L,"function on_background() count=count+1 end");assert(runtime.runBackground());assert(integer(L,"count")==4);
 assert(hp::publishLuaEvent("test-owner","http.completed",123));assert(!hp::LuaApp::dispatchEvents(false));assert(integer(L,"count")==4);
 now+=1000000;assert(hp::LuaApp::dispatchEvents());assert(integer(L,"count")==5);
 assert(!hp::luaBackgroundActive(L));lua_close(L);
}
