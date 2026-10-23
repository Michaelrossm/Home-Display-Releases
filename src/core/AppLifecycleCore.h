#pragma once
#include <cstddef>
#include <cstdint>
namespace hp {
enum class LuaRuntimeState:uint8_t {Foreground,Suspended,Evicted,BackgroundExecuting,Preparing,Opening,Closing,Error};
// Only UI task mutates this tiny LRU. Returning a victim never destroys it here.
template<class T,size_t Capacity=3> class SuspendedAppLru {
public:
    T* entries[Capacity]{};size_t count=0;
    void remove(T* app){for(size_t i=0;i<count;++i)if(entries[i]==app){for(size_t j=i+1;j<count;++j)entries[j-1]=entries[j];entries[--count]=nullptr;return;}}
    T* oldest()const{return count?entries[0]:nullptr;}
    T* add(T* app){remove(app);T* victim=nullptr;if(count==Capacity){victim=entries[0];remove(victim);}entries[count++]=app;return victim;}
};
enum class AppBackgroundPriority:uint8_t {None,Low,Normal,Important};
enum class ForegroundLoad:uint8_t {Idle,Normal,High};
struct AppBackgroundPolicy {
    AppBackgroundPriority priority=AppBackgroundPriority::None;
    uint32_t preferredIntervalMs=1800000,maximumDelayMs=3600000;
    bool requiresNetwork=false,allowDuringHighLoad=false;
};
inline uint32_t minimumBackgroundInterval(AppBackgroundPriority p){return p==AppBackgroundPriority::Important?300000:p==AppBackgroundPriority::Normal?900000:1800000;}
inline bool validBackgroundPolicy(const AppBackgroundPolicy& p){return static_cast<unsigned>(p.priority)<=3&&p.preferredIntervalMs>=minimumBackgroundInterval(p.priority)&&p.preferredIntervalMs<=7*86400000UL&&p.maximumDelayMs>=p.preferredIntervalMs&&p.maximumDelayMs<=7*86400000UL;}
inline AppBackgroundPolicy appPolicy(AppBackgroundPolicy p,bool importantApproved=false){if(p.priority==AppBackgroundPriority::Important&&!importantApproved){p.priority=AppBackgroundPriority::Normal;p.allowDuringHighLoad=false;}if(p.preferredIntervalMs<minimumBackgroundInterval(p.priority))p.preferredIntervalMs=minimumBackgroundInterval(p.priority);if(p.maximumDelayMs<p.preferredIntervalMs)p.maximumDelayMs=p.preferredIntervalMs;return p;}
inline bool backgroundAllowed(const AppBackgroundPolicy& p,ForegroundLoad load,bool online){if(p.priority==AppBackgroundPriority::None||(p.requiresNetwork&&!online))return false;if(load==ForegroundLoad::High)return p.priority==AppBackgroundPriority::Important&&p.allowDuringHighLoad;return true;}
}
