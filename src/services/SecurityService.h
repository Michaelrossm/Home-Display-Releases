#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace hp {
enum class PinKind : uint8_t { Settings, Tablet };
class SecurityService {
public:
    static SecurityService& instance();
    bool begin();
    bool enabled(PinKind kind);
    uint32_t blockedSeconds(PinKind kind);
    bool unlock(PinKind kind,const String& pin);
    bool setPin(PinKind kind,const String& oldPin,const String& pin,const String& repeat);
    bool removePin(PinKind kind,const String& pin);
    void lockSettings();
    void lockTablet();
    bool tabletLocked();
    bool settingsAllowed();
    bool lockAfterDisplayOff();
    bool setLockAfterDisplayOff(bool enabled);
    static bool validPin(const String& pin);
    // Hold through the complete protected write to prevent a lock/check race.
    SemaphoreHandle_t mutex() { return mutex_; }
private:
    struct State { String verifier; uint32_t failures=0,until=0; bool unlocked=false; };
    State states_[2]; SemaphoreHandle_t mutex_=nullptr;
    bool ready_=false,offLock_=true;
    bool verify(PinKind kind,const String& pin);
    bool persistAttempts(PinKind kind,uint32_t seconds);
};
class SecurityWriteGuard {
    SemaphoreHandle_t mutex_;
public:
    SecurityWriteGuard():mutex_(SecurityService::instance().mutex()){if(mutex_)xSemaphoreTakeRecursive(mutex_,portMAX_DELAY);}
    ~SecurityWriteGuard(){if(mutex_)xSemaphoreGiveRecursive(mutex_);}
};
}
