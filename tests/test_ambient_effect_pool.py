from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class AmbientPoolTests(unittest.TestCase):
    def test_actual_pool_reuse_owner_limits_and_cleanup(self):
        header='\n'.join(l for l in (ROOT/'src/ui/UiAmbientAnimationService.h').read_text().splitlines() if not l.startswith('#')).replace('private:','public:')
        source=(ROOT/'src/ui/UiAmbientAnimationService.cpp').read_text(encoding='utf-8')
        allocation=source[source.index('bool UiAmbientAnimationService::hasWeatherEffect'):source.index('bool UiAmbientAnimationService::updateWeatherEffect')]
        cleanup=source[source.index('void UiAmbientAnimationService::release('):source.index('void UiAmbientAnimationService::eventBusCb')]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <vector>
struct lv_obj_t;struct lv_event_t;using Callback=void(*)(lv_event_t*);
struct lv_obj_t{bool valid=true;lv_obj_t* parent=nullptr;std::vector<lv_obj_t*> children;Callback deleted=nullptr;void* data=nullptr;};
struct lv_event_t{void* data;};struct lv_timer_t{};
lv_obj_t objects[1000];int allocated=0,timers=0,timerDeletes=0;
constexpr int LV_OBJ_FLAG_CLICKABLE=1,LV_OBJ_FLAG_HIDDEN=2,LV_EVENT_DELETE=3;
lv_obj_t* lv_obj_create(lv_obj_t* p){auto* o=&objects[allocated++];o->parent=p;if(p)p->children.push_back(o);return o;}
bool lv_obj_is_valid(lv_obj_t* o){return o&&o->valid;}
lv_obj_t* lv_obj_get_parent(lv_obj_t* o){return o->parent;}
void lv_obj_set_size(lv_obj_t*,int,int){}void lv_obj_clear_flag(lv_obj_t*,int){}void lv_obj_add_flag(lv_obj_t*,int){}
void lv_obj_add_event_cb(lv_obj_t* o,Callback cb,int,void* p){o->deleted=cb;o->data=p;}
void* lv_event_get_user_data(lv_event_t* e){return e->data;}
void lv_timer_delete(lv_timer_t*){++timerDeletes;}
void destroy(lv_obj_t* o){assert(o->valid);if(o->deleted){lv_event_t e{o->data};o->deleted(&e);}for(auto* c:o->children)if(c->valid)destroy(c);o->valid=false;}
void transparent(lv_obj_t*){}
constexpr uint8_t kSeedX[8]={7,29,53,79,103,127,149,91},kSeedY[8]={11,37,19,61,43,7,73,29};
constexpr int8_t kDrift[8]={-1,0,1,0,-1,1,0,1};
namespace hp{
class LuaApp{public:size_t used=0,quota=1024;bool reserveUiObjects(size_t n){if(used+n>quota)return false;used+=n;return true;}void releaseUiObjects(size_t n){assert(used>=n);used-=n;}};
class UiTheme{public:static UiTheme& instance(){static UiTheme t;return t;}int accent(){return 0;}};
lv_obj_t* createAppIcon(lv_obj_t* p,const char*,int,int){return lv_obj_create(p);}
}
''' + header + r'''
namespace hp{
UiAmbientAnimationService& UiAmbientAnimationService::instance(){static UiAmbientAnimationService s;return s;}
void UiAmbientAnimationService::ensureStarted(){static lv_timer_t t;if(!timer_){timer_=&t;++timers;}}
void UiAmbientAnimationService::configure(Effect&){}
void UiAmbientAnimationService::reconcileTimer(){}
bool UiAmbientAnimationService::updateWeatherEffect(LuaApp*,lv_obj_t*,const WeatherConfig&,bool,bool,bool,bool){return true;}
''' + allocation + cleanup + r'''
}
int main(){using namespace hp;auto& s=UiAmbientAnimationService::instance();LuaApp a,b,c;
lv_obj_t* first=nullptr;lv_obj_t* roots[32]{};
for(int i=0;i<32;++i){auto* parent=lv_obj_create(nullptr);roots[i]=s.createWeatherEffect(i<16?&a:&b,parent,{});assert(roots[i]);if(i==0)first=parent;}
assert(timers==1&&a.used==160&&b.used==160);
int count=allocated;assert(s.createWeatherEffect(&a,first,{})==roots[0]);assert(count==allocated&&a.used==160);
auto* extra=lv_obj_create(nullptr);assert(!s.createWeatherEffect(&a,extra,{})&&!s.createWeatherEffect(&c,extra,{}));assert(c.used==0);
for(int i=0;i<16;++i)destroy(roots[i]);assert(a.used==0&&b.used==160);
auto* reused=s.createWeatherEffect(&c,extra,{});assert(reused&&c.used==10);
for(int i=16;i<32;++i)destroy(roots[i]);destroy(reused);assert(b.used==0&&c.used==0&&timerDeletes==1);
LuaApp denied;denied.quota=0;assert(!s.createWeatherEffect(&denied,extra,{})&&denied.used==0);
for(const auto& e:s.effects_)assert(!e.used);
}
''')
