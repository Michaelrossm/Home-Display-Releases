#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <type_traits>
#include <cstdio>
class String:public std::string{public:using std::string::string;using std::string::operator=;String(const std::string& s):std::string(s){}template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>String(T n):std::string(std::to_string(n)){}size_t length()const{return size();}};
uint32_t clockMs=0;uint32_t millis(){return clockMs;}int64_t esp_timer_get_time(){return clockMs*1000LL;}
uint32_t micros(){return clockMs*1000u;}
size_t strlcpy(char* out,const char* in,size_t n){const size_t length=strlen(in);if(n){auto count=std::min(n-1,length);memcpy(out,in,count);out[count]=0;}return length;}
using SemaphoreHandle_t=int*;const int pdTRUE=1;const uint32_t portMAX_DELAY=UINT32_MAX;int semaphore=0;SemaphoreHandle_t xSemaphoreCreateMutex(){return &semaphore;}int xSemaphoreTake(SemaphoreHandle_t,uint32_t){return 1;}void xSemaphoreGive(SemaphoreHandle_t){}
constexpr int LV_PART_MAIN=0,LV_OBJ_FLAG_HIDDEN=1,LV_OPA_COVER=255,MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_SPIRAM=4;
#define LV_PCT(x) x
struct lv_obj_t{lv_obj_t* parent=nullptr;std::vector<lv_obj_t*> children;bool hidden=false,valid=true;};
lv_obj_t baseScreen;lv_obj_t* lv_layer_bottom(){return &baseScreen;}lv_obj_t* lv_screen_active(){return &baseScreen;}
lv_obj_t* lv_obj_create(lv_obj_t* parent){auto* obj=new lv_obj_t;obj->parent=parent;if(parent)parent->children.push_back(obj);return obj;}
void lv_obj_set_parent(lv_obj_t* obj,lv_obj_t* parent){if(obj->parent){auto& v=obj->parent->children;v.erase(std::remove(v.begin(),v.end(),obj),v.end());}obj->parent=parent;if(parent)parent->children.push_back(obj);}
void lv_obj_delete(lv_obj_t* obj){while(!obj->children.empty())lv_obj_delete(obj->children.back());lv_obj_set_parent(obj,nullptr);obj->valid=false;}
void lv_obj_clean(lv_obj_t* obj){while(!obj->children.empty())lv_obj_delete(obj->children.back());}
bool lv_obj_is_valid(lv_obj_t* obj){return obj&&obj->valid;}lv_obj_t* lv_obj_get_parent(lv_obj_t* obj){return obj->parent;}uint32_t lv_obj_get_child_count(lv_obj_t* obj){return obj->children.size();}lv_obj_t* lv_obj_get_child(lv_obj_t* obj,uint32_t i){return obj->children.at(i);}
bool lv_obj_has_flag(lv_obj_t* obj,int){return obj->hidden;}void lv_obj_add_flag(lv_obj_t* obj,int){obj->hidden=true;}void lv_obj_remove_flag(lv_obj_t* obj,int){obj->hidden=false;}
int lv_obj_get_style_opa(lv_obj_t*,int){return 255;}int lv_obj_get_x(lv_obj_t*){return 0;}int lv_obj_get_y(lv_obj_t*){return 0;}int lv_obj_get_width(lv_obj_t*){return 1280;}int lv_obj_get_height(lv_obj_t*){return 800;}
void lv_obj_update_layout(lv_obj_t*){}void lv_obj_invalidate(lv_obj_t*){}void lv_obj_set_size(lv_obj_t*,int,int){}int lv_color_hex(int color){return color;}
uint32_t lv_obj_get_index(lv_obj_t* obj){auto& children=obj->parent->children;return std::find(children.begin(),children.end(),obj)-children.begin();}
void lv_obj_set_style_bg_color(lv_obj_t*,int,int){}void lv_obj_set_style_bg_opa(lv_obj_t*,int,int){}void lv_obj_set_style_border_width(lv_obj_t*,int,int){}void lv_obj_set_style_text_color(lv_obj_t*,int,int){}void lv_obj_set_style_text_font(lv_obj_t*,const void*,int){}void lv_obj_center(lv_obj_t*){}
lv_obj_t* lv_label_create(lv_obj_t* obj){return lv_obj_create(obj);}void lv_label_set_text_fmt(lv_obj_t*,const char*,...){}int lv_font_montserrat_24=0;const void* fontWithGermanFallback(const void* p){return p;}
void lv_anim_delete(lv_obj_t*,void*){}struct lv_timer_t{bool paused=false;};void lv_timer_pause(lv_timer_t* timer){timer->paused=true;}void lv_timer_resume(lv_timer_t* timer){timer->paused=false;}
struct SerialStub{void printf(const char*,...){}}Serial;
bool lowMemory=false;size_t heap_caps_get_free_size(int flags){return lowMemory?0:flags&MALLOC_CAP_SPIRAM?16*1024*1024:128*1024;}
#include "AppManager.h"
#include "AppBackgroundScheduler.h"
// This fixture exercises manager/object lifecycle. Registry dispatch itself is
// covered by the pinned real-Lua tests in lua_vnext.inc.
constexpr int LUA_REGISTRYINDEX=-10000,LUA_OK=0;
void lua_getfield(void*,int,const char*){}void lua_remove(void*,int){}void lua_pop(void*,int){}
int protectedCall(void*,int,int,unsigned,int){return LUA_OK;}
#include <ArduinoJson.h>
namespace hp{
struct VideoService{static VideoService& instance(){static VideoService v;return v;}void close(const char*){}};
struct PowerManager{static PowerManager& instance(){static PowerManager p;return p;}void requestBurst(uint32_t){}};
struct UiPerformanceMonitor{static UiPerformanceMonitor& instance(){static UiPerformanceMonitor p;return p;}void navigationQueued(bool=false){}void navigationFailed(){}void navigationBuilt(){}void appBuild(uint32_t,bool=false){}};
class EventLog{public:static EventLog& instance(){static EventLog e;return e;}void add(const char*,const char*,const String&){}void addTransient(const char*,const char*,const String&){} };
enum class EventType{AppOpened};struct Event{EventType type;IApp* app;};class EventBus{public:static EventBus& instance(){static EventBus b;return b;}void publish(Event){} };
class SecurityService{public:static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return false;}void lockSettings(){}};
class NetworkService{public:bool connected=true;static NetworkService& instance(){static NetworkService n;return n;}bool online(){return connected;}};
class NetworkWorker{public:using Handler=void(*)(void*);Handler callback=nullptr;void* context=nullptr;static NetworkWorker& instance(){static NetworkWorker w;return w;}bool submit(Handler h,void* c,const char*){if(callback)return false;callback=h;context=c;return true;}void drain(){auto h=callback;callback=nullptr;if(h)h(context);}};
class ConfigService{std::map<std::string,bool> values;public:
enum class AppStateWriteResult {Ok,NotWritable,InvalidIdentity,InvalidRecord,WriteFailed};
static ConfigService& instance(){static ConfigService c;return c;}
bool appEnabled(const char* id,bool fallback){return values.count(id)?values[id]:fallback;}
AppStateWriteResult saveAppEnabled(const char* id,bool value){values[id]=value;return AppStateWriteResult::Ok;}};
class UiTheme{public:static UiTheme& instance(){static UiTheme t;return t;}void styleScreen(lv_obj_t*){}};class UiShell{public:static UiShell& instance(){static UiShell s;return s;}void hideHomeBackground(lv_obj_t*){}void showHome(){}};
class LuaJobService{public:int cancellations=0;static LuaJobService& instance(){static LuaJobService j;return j;}void cancelOwner(const char*,bool=false){++cancellations;}};
class LuaApp:public IApp{String id_;public:void* L_=nullptr;lv_obj_t* root_=nullptr;lv_timer_t* timer_=nullptr;bool paused_=false,settingsState_=false;LuaRuntimeState lifecycle_=LuaRuntimeState::Evicted;int opens=0,resumes=0,suspends=0,evicts=0,closes=0;std::map<std::string,int> callbacks;
 bool faulted_=false;void quarantine(const char* callback);
 explicit LuaApp(const char* id):id_(id){}const char* id()const override{return id_.c_str();}const char* name()const override{return id();}const char* icon()const override{return "";}AppKind kind()const override{return AppKind::Lua;}bool begin()override{return true;}bool userInstallable()const override{return true;}bool enabledByDefault()const override{return true;}bool hasSettings()const override{return true;}
 void open(lv_obj_t* parent)override{++opens;faulted_=false;L_=reinterpret_cast<void*>(static_cast<uintptr_t>(opens));root_=lv_obj_create(parent);timer_=new lv_timer_t;paused_=false;settingsState_=false;lifecycle_=LuaRuntimeState::Foreground;}
 bool openSettings(lv_obj_t* parent)override{open(parent);settingsState_=true;return true;}
 void close()override{++closes;if(timer_){delete timer_;timer_=nullptr;}if(root_&&lv_obj_is_valid(root_))lv_obj_delete(root_);root_=nullptr;L_=nullptr;paused_=false;settingsState_=false;lifecycle_=LuaRuntimeState::Evicted;LuaJobService::instance().cancelOwner(id());AppBackgroundScheduler::instance().remove(id());}
 String failingCallback;bool hasGlobal(const char*)const{return true;}
 bool callGlobal(const char* name,uint32_t=30,int=250){++callbacks[name];return failingCallback!=name;}void pause();void resume();bool suspendTo(lv_obj_t* parking);bool resumeTo(lv_obj_t* parent);void evict();
 bool settingsState()const{return settingsState_;}LuaRuntimeState lifecycle()const{return lifecycle_;}
 bool opening()const{return false;}bool failed()const{return faulted_;}void serviceUi(){}
};
#include "lifecycle.inc"
}
#include "manager.inc"
namespace hp{
#include "background.inc"
bool backgroundManifest(const char* json,AppBackgroundPolicy& policy){JsonDocument doc;String error;if(deserializeJson(doc,json))return false;struct Parsed{uint16_t schema=3;AppBackgroundPolicy background;}out;
#include "manifest.inc"
 policy=out.background;return true;}
}
int executed=0;std::vector<std::string> order;void handler(const char* owner){++executed;order.push_back(owner);}
int main(){using namespace hp;auto& bg=AppBackgroundScheduler::instance();assert(bg.begin());auto& manager=AppManager::instance();lv_obj_t* host=lv_obj_create(&baseScreen);LuaApp a("a"),b("b"),c("c"),d("d"),e("e");for(auto* app:{&a,&b,&c,&d,&e})assert(manager.registerApp(app));
 assert(manager.open("a",host));auto* original=a.L_;auto* originalRoot=a.root_;assert(manager.open("b",host));assert(a.lifecycle()==LuaRuntimeState::Suspended&&a.paused_&&a.timer_->paused&&a.root_->hidden&&a.callbacks["on_suspend"]==1);assert(b.lifecycle()==LuaRuntimeState::Foreground&&!b.paused_);
 assert(manager.open("a",host));assert(a.L_==original&&a.root_==originalRoot&&a.opens==1&&a.callbacks["on_resume"]==1&&!a.timer_->paused&&!a.root_->hidden);
 assert(manager.openSettings("a",host));assert(a.settingsState());const int settingsClose=a.closes;assert(manager.open("b",host));assert(a.L_==nullptr&&a.closes>settingsClose&&manager.runtimeStatus("a").state==AppRuntimeState::Closed);
 assert(manager.open("a",host));assert(manager.open("c",host));assert(manager.open("d",host));assert(manager.open("e",host));assert(b.lifecycle()==LuaRuntimeState::Evicted&&b.callbacks["on_evict"]==1);assert(e.lifecycle()==LuaRuntimeState::Foreground&&manager.luaDiagnostics().suspended==3);
 lowMemory=true;manager.suspendCurrent();assert(!manager.current()&&manager.luaDiagnostics().suspended==0);lowMemory=false;
 assert(manager.open("a",host));assert(manager.setEnabled("b",false));assert(!manager.open("b",host));assert(manager.setEnabled("b",true));assert(manager.open("b",host));assert(manager.unregisterApp("a")&&a.L_==nullptr);
 AppBackgroundPolicy low;low.priority=AppBackgroundPriority::Low;low.preferredIntervalMs=1800000;low.maximumDelayMs=3600000;assert(bg.registerNative("low",low,handler));AppBackgroundPolicy important;important.priority=AppBackgroundPriority::Important;important.preferredIntervalMs=300000;important.maximumDelayMs=600000;important.allowDuringHighLoad=true;assert(bg.registerNative("important",important,handler,true));
 assert(!bg.request("bad",AppBackgroundPolicy{AppBackgroundPriority::Low,1000,1000,false,false}));assert(bg.request("lua",important));bg.remove("lua");
 bg.setForeground(nullptr);bg.setForegroundLoad(ForegroundLoad::High);clockMs=1800000;bg.backgroundTick();assert(executed==1&&order.back()=="important");AppBackgroundScheduler::Diagnostics diag;assert(bg.tryDiagnostics(diag)&&diag.deferredHigh>0);bg.remove("important");bg.setForegroundLoad(ForegroundLoad::Idle);bg.backgroundTick();assert(executed==2&&order.back()=="low");
 low.requiresNetwork=true;assert(bg.registerNative("network",low,handler));NetworkService::instance().connected=false;clockMs+=1800000;bg.backgroundTick();assert(bg.tryDiagnostics(diag)&&diag.deferredOffline>0);NetworkService::instance().connected=true;bg.backgroundTick();NetworkWorker::instance().drain();assert(order.back()=="network");
 bg.remove("low");clockMs+=1800000;bg.backgroundTick();const int before=executed;bg.remove("network");NetworkWorker::instance().drain();assert(executed==before);clockMs+=7*86400000;bg.backgroundTick();assert(executed==before);assert(LuaJobService::instance().cancellations>0);
 AppBackgroundPolicy parsed;assert(backgroundManifest("{}",parsed)&&parsed.priority==AppBackgroundPriority::None);
 assert(backgroundManifest(R"json({"background":{"enabled":true,"priority":"low","preferredIntervalMinutes":180,"maximumDelayMinutes":360,"requiresNetwork":true}})json",parsed));assert(parsed.priority==AppBackgroundPriority::Low&&parsed.requiresNetwork&&parsed.preferredIntervalMs==10800000);
 assert(!backgroundManifest(R"json({"background":{"enabled":true,"priority":"critical"}})json",parsed));assert(!backgroundManifest(R"json({"background":{"enabled":true,"priority":"low","preferredIntervalMinutes":1}})json",parsed));assert(!backgroundManifest(R"json({"background":{"enabled":"true","priority":"low"}})json",parsed));assert(!backgroundManifest(R"json({"background":{"enabled":true,"priority":"low","unknown":1}})json",parsed));assert(!backgroundManifest(R"json({"background":null})json",parsed));
 assert(backgroundManifest(R"json({"background":{"enabled":true,"priority":"important","preferredIntervalMinutes":5,"maximumDelayMinutes":10,"allowDuringHighLoad":true}})json",parsed));assert(parsed.priority==AppBackgroundPriority::Normal&&!parsed.allowDuringHighLoad&&parsed.preferredIntervalMs==900000);
 // A quarantined app keeps the same view/state; no automatic screen reload.
 assert(manager.open("b",host));auto* frozenRoot=b.root_;auto* frozenState=b.L_;
 const int closes=b.closes,opens=b.opens,cancelled=LuaJobService::instance().cancellations;
 b.quarantine("on_tick");assert(b.faulted_&&b.paused_&&b.timer_->paused);
 assert(b.root_==frozenRoot&&b.L_==frozenState&&b.root_->valid&&!b.root_->hidden);
 b.resume();assert(b.paused_&&b.timer_->paused);
 assert(!b.suspendTo(&baseScreen)&&!b.resumeTo(host));
 b.quarantine("on_event");assert(LuaJobService::instance().cancellations==cancelled+1);
 assert(b.closes==closes&&b.opens==opens);
 assert(manager.open("c",host));assert(b.L_==nullptr);
 assert(manager.open("b",host));assert(!b.faulted_&&!b.paused_);
 char luaOwner[32]{};low.requiresNetwork=false;bg.setForeground(nullptr);bg.setForegroundLoad(ForegroundLoad::Idle);
 assert(bg.request("c",low));clockMs+=1800000;bg.backgroundTick();assert(bg.luaPending());
 assert(bg.takeLuaOwner(luaOwner)&&!strcmp(luaOwner,"c"));assert(!bg.takeLuaOwner(luaOwner));bg.finishLua(25);
 bg.backgroundTick();assert(!bg.luaPending());
 clockMs+=1800000;bg.setForegroundLoad(ForegroundLoad::High);bg.backgroundTick();assert(!bg.luaPending());
 bg.setForegroundLoad(ForegroundLoad::Idle);bg.backgroundTick();assert(bg.luaPending());assert(manager.setEnabled("c",false));assert(!bg.luaPending()&&!bg.hasPolicy("c"));
 assert(manager.setEnabled("c",true));low.requiresNetwork=true;assert(bg.request("c",low));clockMs+=1800000;
 NetworkService::instance().connected=false;bg.backgroundTick();assert(!bg.luaPending());NetworkService::instance().connected=true;
 bg.backgroundTick();assert(bg.luaPending()&&!NetworkWorker::instance().callback);c.evict();assert(!bg.luaPending()&&!bg.hasPolicy("c"));
 assert(bg.request("d",low));clockMs+=1800000;bg.backgroundTick();assert(bg.luaPending());assert(manager.unregisterApp("d"));assert(!bg.luaPending()&&!bg.hasPolicy("d"));
 assert(bg.request("e",low));clockMs+=1800000;bg.backgroundTick();assert(bg.luaPending());AppBackgroundPolicy none;
 assert(bg.request("e",none));assert(!bg.luaPending());assert(bg.request("e",low));clockMs+=1800000;bg.backgroundTick();assert(bg.luaPending());bg.remove("e");
 // Disable cleanup is deferred to the guarded UI pass, not a web callback.
 assert(manager.setEnabled("b",true)&&manager.setEnabled("c",true));
 assert(manager.open("b",host)&&manager.open("c",host));assert(b.lifecycle()==LuaRuntimeState::Suspended);
 auto* disabledState=b.L_;assert(manager.setEnabled("b",false));assert(b.L_==disabledState);
 manager.serviceCurrentUi();assert(b.L_==nullptr&&b.lifecycle()==LuaRuntimeState::Evicted&&manager.current()==&c);
 assert(manager.setEnabled("c",false));manager.serviceCurrentUi();assert(!manager.current()&&c.L_==nullptr);
 assert(manager.find("c")==&c&&!manager.open("c",host));assert(manager.setEnabled("c",true)&&manager.open("c",host));
 c.failingCallback="on_suspend";manager.suspendCurrent();assert(!manager.current()&&!c.L_);
 c.failingCallback="";assert(manager.open("c",host));assert(c.suspendTo(&baseScreen));
 c.failingCallback="on_resume";assert(!c.resumeTo(host)&&c.faulted_&&c.paused_&&c.root_->valid);
 printf("PASS production manager, lifecycle methods, quarantine, LRU, manifest and scheduling policy\n");return 0;}
