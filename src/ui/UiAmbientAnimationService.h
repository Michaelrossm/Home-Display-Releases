#pragma once
#include <lvgl.h>
#include <cstdint>
namespace hp {
class LuaApp;
struct Event;
class UiAmbientAnimationService {
public:
    enum class WeatherType:uint8_t{Clear,PartlyCloudy,Cloudy,Fog,Drizzle,Rain,HeavyRain,Snow,Storm,Wind,ClearNight};
    struct WeatherConfig{WeatherType type=WeatherType::Clear;uint8_t intensity=30;uint16_t width=140,height=100;bool enabled=true;};
    static UiAmbientAnimationService& instance();
    static WeatherType parseWeatherType(const char*);
    static const char* weatherSymbol(WeatherType);
    bool hasWeatherEffect(LuaApp* owner,lv_obj_t* parent) const;
    lv_obj_t* createWeatherEffect(LuaApp*,lv_obj_t*,const WeatherConfig&);
    bool updateWeatherEffect(LuaApp*,lv_obj_t*,const WeatherConfig&,bool,bool,bool,bool);
    void uiStateChanged(){reconcileTimer();}
private:
    static constexpr uint8_t kMaxEffects=32;
    static constexpr uint8_t kMaxOwnerEffects=16;
    static constexpr uint8_t kMaxParticles=8;
    struct Particle{lv_obj_t* object=nullptr;int16_t x=0,y=0;int8_t vx=0,vy=0;uint8_t phase=0;};
    struct Effect{bool used=false,enabled=true;LuaApp* owner=nullptr;lv_obj_t* root=nullptr;lv_obj_t* icon=nullptr;WeatherType type=WeatherType::Clear;uint8_t intensity=30;uint16_t width=140,height=100,themeGeneration=0;uint8_t activeParticles=0,frame=0;Particle particles[kMaxParticles]{};};
    static void timerCb(lv_timer_t*);
    static void rootDeleted(lv_event_t*);
    static void eventBusCb(const Event&);
    void tick();void ensureStarted();void reconcileTimer();
    bool anyForegroundEffect()const;bool effectVisible(const Effect&)const;bool effectRunnable(const Effect&)const;
    Effect* find(LuaApp*,lv_obj_t*);Effect* findByRoot(lv_obj_t*);Effect* allocate(LuaApp*);
    void release(Effect&);void configure(Effect&);void applyTheme(Effect&);void layoutParticles(Effect&);void animate(Effect&);
    Effect effects_[kMaxEffects]{};lv_timer_t* timer_=nullptr;bool subscribed_=false;
};
}
