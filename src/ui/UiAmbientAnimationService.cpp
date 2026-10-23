#include "UiAmbientAnimationService.h"
#include "UiTheme.h"
#include "NativeAppIcon.h"
#include "../apps/lua/LuaApp.h"
#include "../services/IdleService.h"
#include "../core/EventBus.h"
#include <algorithm>
#include <cstring>

namespace hp {
namespace {
constexpr uint8_t kSeedX[8]={7,29,53,79,103,127,149,91};
constexpr uint8_t kSeedY[8]={11,37,19,61,43,7,73,29};
constexpr int8_t kDrift[8]={-1,0,1,0,-1,1,0,1};

uint8_t scaledCount(uint8_t lo,uint8_t hi,uint8_t intensity){
    return hi<=lo?lo:static_cast<uint8_t>(lo+((hi-lo)*std::min<uint8_t>(100,intensity))/100);
}
bool hiddenInTree(lv_obj_t* o){
    for(auto* p=o;p;p=lv_obj_get_parent(p))if(lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN))return true;
    return false;
}
void transparent(lv_obj_t* o){
    lv_obj_remove_style_all(o);lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_pad_all(o,0,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);
}
}

UiAmbientAnimationService& UiAmbientAnimationService::instance(){static UiAmbientAnimationService s;return s;}

UiAmbientAnimationService::WeatherType UiAmbientAnimationService::parseWeatherType(const char* v){
    if(!v)return WeatherType::Clear;
    if(!strcmp(v,"partly_cloudy"))return WeatherType::PartlyCloudy;
    if(!strcmp(v,"cloudy"))return WeatherType::Cloudy;
    if(!strcmp(v,"fog"))return WeatherType::Fog;
    if(!strcmp(v,"drizzle"))return WeatherType::Drizzle;
    if(!strcmp(v,"rain"))return WeatherType::Rain;
    if(!strcmp(v,"heavy_rain"))return WeatherType::HeavyRain;
    if(!strcmp(v,"snow"))return WeatherType::Snow;
    if(!strcmp(v,"storm"))return WeatherType::Storm;
    if(!strcmp(v,"wind"))return WeatherType::Wind;
    if(!strcmp(v,"clear_night"))return WeatherType::ClearNight;
    return WeatherType::Clear;
}
const char* UiAmbientAnimationService::weatherSymbol(WeatherType t){
    switch(t){
        case WeatherType::Clear:return "☀";
        case WeatherType::PartlyCloudy:return "⛅";
        case WeatherType::Cloudy:return "☁";
        case WeatherType::Fog:return "";
        case WeatherType::Drizzle:return "🌦";
        case WeatherType::Rain:return "🌧";
        case WeatherType::HeavyRain:return "🌧";
        case WeatherType::Snow:return "🌨";
        case WeatherType::Storm:return "⛈";
        case WeatherType::Wind:return "💨";
        case WeatherType::ClearNight:return "🌙";
    }
    return "☀";
}
void UiAmbientAnimationService::ensureStarted(){
    if(!subscribed_){EventBus::instance().subscribe(eventBusCb);subscribed_=true;}
    if(!timer_){timer_=lv_timer_create(timerCb,83,this);lv_timer_pause(timer_);}
}
bool UiAmbientAnimationService::hasWeatherEffect(LuaApp* owner,lv_obj_t* parent) const{
    if(!owner)return false;
    for(const auto& e:effects_)if(e.used&&e.owner==owner&&e.root&&lv_obj_is_valid(e.root)&&lv_obj_get_parent(e.root)==parent)return true;
    return false;
}
UiAmbientAnimationService::Effect* UiAmbientAnimationService::find(LuaApp* owner,lv_obj_t* parent){
    for(auto& e:effects_)if(e.used&&e.owner==owner&&e.root&&lv_obj_get_parent(e.root)==parent)return &e;return nullptr;
}
UiAmbientAnimationService::Effect* UiAmbientAnimationService::findByRoot(lv_obj_t* root){
    for(auto& e:effects_)if(e.used&&e.root==root)return &e;return nullptr;
}
UiAmbientAnimationService::Effect* UiAmbientAnimationService::allocate(LuaApp* owner){
    uint8_t count=0;for(const auto& e:effects_)if(e.used&&e.owner==owner)++count;
    if(count>=kMaxOwnerEffects)return nullptr;
    for(auto& e:effects_)if(!e.used){e={};e.used=true;e.owner=owner;return &e;}
    return nullptr;
}
lv_obj_t* UiAmbientAnimationService::createWeatherEffect(LuaApp* owner,lv_obj_t* parent,const WeatherConfig& in){
    if(!owner||!parent||!lv_obj_is_valid(parent))return nullptr;
    ensureStarted();
    if(auto* existing=find(owner,parent)){
        if(existing->root&&lv_obj_is_valid(existing->root)){
            updateWeatherEffect(owner,existing->root,in,true,true,true,true);return existing->root;
        }
        release(*existing);
    }
    auto* e=allocate(owner);if(!e)return nullptr;
    if(!owner->reserveUiObjects(10)){release(*e);return nullptr;}
    e->type=in.type;e->intensity=std::min<uint8_t>(100,in.intensity);e->enabled=in.enabled;
    e->width=std::max<uint16_t>(32,std::min<uint16_t>(160,in.width));
    e->height=std::max<uint16_t>(28,std::min<uint16_t>(120,in.height));
    e->root=lv_obj_create(parent);if(!e->root){owner->releaseUiObjects(10);release(*e);return nullptr;}
    transparent(e->root);lv_obj_set_size(e->root,e->width,e->height);lv_obj_add_event_cb(e->root,rootDeleted,LV_EVENT_DELETE,e);
    e->icon=createAppIcon(e->root,"@weather_cloudy",std::min<int>(72,std::min(e->width,e->height)-4),UiTheme::instance().accent());
    lv_obj_clear_flag(e->icon,LV_OBJ_FLAG_CLICKABLE);
    for(uint8_t i=0;i<8;++i){auto& p=e->particles[i];p.object=lv_obj_create(e->root);transparent(p.object);
        p.x=kSeedX[i];p.y=kSeedY[i];p.vx=kDrift[i];p.vy=1;p.phase=i*29u;lv_obj_add_flag(p.object,LV_OBJ_FLAG_HIDDEN);}
    configure(*e);reconcileTimer();return e->root;
}
bool UiAmbientAnimationService::updateWeatherEffect(LuaApp* owner,lv_obj_t* handle,const WeatherConfig& in,bool ut,bool ui,bool us,bool ue){
    auto* e=findByRoot(handle);if(!e||e->owner!=owner||!lv_obj_is_valid(handle))return false;
    if(ut)e->type=in.type;if(ui)e->intensity=std::min<uint8_t>(100,in.intensity);
    if(us){e->width=std::max<uint16_t>(32,std::min<uint16_t>(160,in.width));e->height=std::max<uint16_t>(28,std::min<uint16_t>(120,in.height));lv_obj_set_size(e->root,e->width,e->height);lv_obj_set_size(e->icon,std::min<int>(72,std::min(e->width,e->height)-4),std::min<int>(72,std::min(e->width,e->height)-4));}
    if(ue)e->enabled=in.enabled;
    configure(*e);reconcileTimer();return true;
}
void UiAmbientAnimationService::configure(Effect& e){
    e.activeParticles=0;e.frame=0;
    lv_obj_set_style_opa(e.icon,LV_OPA_COVER,0);
    switch(e.type){
        case WeatherType::Fog:case WeatherType::Wind:case WeatherType::Drizzle:e.activeParticles=3;break;
        case WeatherType::Rain:e.activeParticles=scaledCount(3,6,e.intensity);break;
        case WeatherType::HeavyRain:e.activeParticles=scaledCount(5,8,e.intensity);break;
        case WeatherType::Snow:e.activeParticles=scaledCount(3,6,e.intensity);break;
        case WeatherType::Storm:e.activeParticles=scaledCount(4,7,e.intensity);break;
        default:break;
    }
    const char* markers[]={"@weather_clear","@weather_partly_cloudy","@weather_cloudy","@weather_fog","@weather_drizzle","@weather_rain","@weather_heavy_rain","@weather_snow","@weather_storm","@weather_wind","@weather_clear_night"};
    setAppIconSymbol(e.icon,markers[static_cast<unsigned>(e.type)]);
    if(!e.enabled)lv_obj_add_flag(e.icon,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(e.icon,LV_OBJ_FLAG_HIDDEN);
    for(uint8_t i=0;i<8;++i){auto& p=e.particles[i];
        if(i<e.activeParticles&&e.enabled)lv_obj_remove_flag(p.object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(p.object,LV_OBJ_FLAG_HIDDEN);
        p.x=kSeedX[i]%std::max<uint16_t>(1,e.width);p.y=kSeedY[i]%std::max<uint16_t>(1,e.height);p.vx=kDrift[i];p.vy=1+(i&1);}
    applyTheme(e);layoutParticles(e);
}
void UiAmbientAnimationService::applyTheme(Effect& e){
    auto& t=UiTheme::instance();e.themeGeneration=t.generation();lv_color_t icon=t.textSecondary();
    switch(e.type){
        case WeatherType::Clear:case WeatherType::PartlyCloudy:case WeatherType::Storm:icon=t.warning();break;
        case WeatherType::Drizzle:case WeatherType::Rain:case WeatherType::HeavyRain:case WeatherType::Wind:icon=t.accent();break;
        default:break;
    }
    lv_obj_set_style_text_color(e.icon,icon,0);
    for(uint8_t i=0;i<8;++i){auto* o=e.particles[i].object;lv_color_t c=t.accent();
        if(e.type==WeatherType::Rain||e.type==WeatherType::Drizzle||e.type==WeatherType::HeavyRain||e.type==WeatherType::Storm)c=lv_color_hex(0x4DBCF5);
        if(e.type==WeatherType::Fog||e.type==WeatherType::Snow)c=t.textSecondary();
        lv_obj_set_style_bg_color(o,c,0);lv_obj_set_style_bg_opa(o,e.type==WeatherType::Fog?LV_OPA_50:LV_OPA_80,0);}
}
void UiAmbientAnimationService::layoutParticles(Effect& e){
    lv_obj_align(e.icon,LV_ALIGN_CENTER,0,0);
    const int w=e.width,h=e.height;
    for(uint8_t i=0;i<8;++i){auto& p=e.particles[i];auto* o=p.object;
        if(e.type==WeatherType::Fog||e.type==WeatherType::Wind){
            const int ww=e.type==WeatherType::Fog?std::max(34,w-34-int(i)*13):std::max(24,w/2-int(i)*8);
            lv_obj_set_size(o,ww,e.type==WeatherType::Fog?3:2);lv_obj_set_style_radius(o,2,0);
            p.x=(i*21)%std::max(1,w-ww);p.y=h/3+int(i)*std::max(4,h/5);p.vx=e.type==WeatherType::Wind?3:(i&1?-1:1);p.vy=0;
        }else if(e.type==WeatherType::Snow){
            const int s=4+(i&1);lv_obj_set_size(o,s,s);lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);p.vy=1;
        }else{
            const bool heavy=e.type==WeatherType::HeavyRain||e.type==WeatherType::Storm;
            lv_obj_set_size(o,heavy?3:2,heavy?13:9);lv_obj_set_style_radius(o,2,0);p.vy=heavy?3:(e.type==WeatherType::Drizzle?1:2);
        }
        lv_obj_set_pos(o,p.x,p.y);
    }
}
bool UiAmbientAnimationService::effectVisible(const Effect& e)const{return e.root&&lv_obj_is_valid(e.root)&&!hiddenInTree(e.root);}
bool UiAmbientAnimationService::effectRunnable(const Effect& e)const{return e.used&&e.enabled&&e.owner&&e.owner->uiAnimationEligible()&&effectVisible(e);}
bool UiAmbientAnimationService::anyForegroundEffect()const{
    for(const auto& e:effects_)if(e.used&&e.owner&&e.owner->lifecycle()==LuaRuntimeState::Foreground&&effectRunnable(e))return true;
    return false;
}
void UiAmbientAnimationService::animate(Effect& e){
    ++e.frame;if(e.themeGeneration!=UiTheme::instance().generation())applyTheme(e);
    if(e.type==WeatherType::Clear||e.type==WeatherType::ClearNight){
        const uint8_t p=e.frame&31u,q=p<16?p:31u-p;lv_obj_set_style_opa(e.icon,static_cast<lv_opa_t>(215+q*2),0);return;
    }
    if(e.type==WeatherType::PartlyCloudy||e.type==WeatherType::Cloudy){
        const uint8_t p=e.frame&31u;const int off=p<16?int(p)/4:int(31u-p)/4;lv_obj_align(e.icon,LV_ALIGN_CENTER,off-2,0);return;
    }
    if(e.type==WeatherType::Fog||e.type==WeatherType::Wind){
        for(uint8_t i=0;i<e.activeParticles;++i){auto& p=e.particles[i];const int ww=lv_obj_get_width(p.object);p.x+=p.vx;
            if(p.vx>0&&p.x>e.width)p.x=-ww;else if(p.vx<0&&p.x+ww<0)p.x=e.width;lv_obj_set_x(p.object,p.x);}return;
    }
    for(uint8_t i=0;i<e.activeParticles;++i){auto& p=e.particles[i];p.y+=p.vy;
        if(e.type==WeatherType::Snow){p.x+=kDrift[(e.frame/8u+i)&7u];if(p.x<0)p.x=e.width-1;else if(p.x>=e.width)p.x=0;}
        if(p.y>e.height){p.y=-4-int(i)*5;p.x=(kSeedX[(i+e.frame)&7u]+i*17u)%std::max<uint16_t>(1,e.width);}
        lv_obj_set_pos(p.object,p.x,p.y);}
    if(e.type==WeatherType::Storm){const uint8_t c=e.frame%96u;lv_obj_set_style_opa(e.icon,(c==0u||c==1u)?LV_OPA_50:LV_OPA_COVER,0);}
}
void UiAmbientAnimationService::tick(){
    const auto stage=IdleService::instance().stage();if(stage==IdleStage::Saver||stage==IdleStage::Off){reconcileTimer();return;}
    bool ran=false;for(auto& e:effects_)if(effectRunnable(e)){animate(e);ran=true;}if(!ran)reconcileTimer();
}
void UiAmbientAnimationService::timerCb(lv_timer_t* t){auto* s=static_cast<UiAmbientAnimationService*>(lv_timer_get_user_data(t));if(s)s->tick();}
void UiAmbientAnimationService::reconcileTimer(){
    if(!timer_)return;const auto stage=IdleService::instance().stage();
    if(stage==IdleStage::Saver||stage==IdleStage::Off||!anyForegroundEffect()){lv_timer_pause(timer_);return;}
    uint8_t visible=0;for(const auto& e:effects_)if(effectRunnable(e))++visible;
    lv_timer_set_period(timer_,stage==IdleStage::Dimmed?220:visible>1?125:83);lv_timer_resume(timer_);
}
void UiAmbientAnimationService::release(Effect& e){e.used=false;e.enabled=false;e.owner=nullptr;e.root=nullptr;e.icon=nullptr;e.activeParticles=0;for(auto& p:e.particles)p={};}
void UiAmbientAnimationService::rootDeleted(lv_event_t* ev){
    auto* e=static_cast<Effect*>(lv_event_get_user_data(ev));if(!e)return;auto& s=instance();if(e->owner)e->owner->releaseUiObjects(10);s.release(*e);
    bool any=false;for(const auto& x:s.effects_)if(x.used){any=true;break;}
    if(!any&&s.timer_){lv_timer_delete(s.timer_);s.timer_=nullptr;}else s.reconcileTimer();
}
void UiAmbientAnimationService::eventBusCb(const Event& ev){
    if(ev.type!=EventType::IdleStateChanged&&ev.type!=EventType::AppOpened)return;auto& s=instance();if(s.timer_)s.reconcileTimer();
}
}
