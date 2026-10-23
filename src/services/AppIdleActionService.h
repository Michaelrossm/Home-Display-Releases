#pragma once
#include <Arduino.h>
#include <atomic>
namespace hp {
// Generic app launch policy; no slideshow/photo data and no extra task.
class AppIdleActionService {
public:
    static AppIdleActionService& instance();
    bool configure(const String& owner,uint32_t after,const String& arguments,bool enabled);
    void poll(); // Existing UI pass, under DisplayUiGuard.
private:
    std::atomic<bool> dirty_{true};String owner_,arguments_;uint32_t after_=0,lastCheck_=0,lastFired_=0;bool running_=false,haveFired_=false;
};
}
