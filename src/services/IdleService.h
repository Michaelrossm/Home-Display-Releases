#pragma once
#include <Arduino.h>
#include <atomic>
#include "SettingsService.h"

namespace hp {

enum class IdleContext : uint8_t { Normal, Media, Power };
enum class IdleStage : uint8_t { Active, Dimmed, Saver, Off };
enum class IdleLease : uint8_t { PhotoFrame,PhotoViewer,FirmwareUpdate,Count };

class IdleService {
public:
    static IdleService& instance();

    void begin();
    void loop();

    // May be called from the LVGL/input task as well as the main task.
    // A generation counter guarantees that an already-running idle check can
    // never overwrite a newer physical/user activity.
    void activity();
    uint32_t lastActivityMillis()const{return lastActivity_.load(std::memory_order_acquire);}
    bool idleActionActive()const{return idleActionActive_.load(std::memory_order_acquire);}
    void beginIdleAction(){idleActionActive_.store(true,std::memory_order_release);activityGeneration_.fetch_add(1,std::memory_order_acq_rel);change(IdleStage::Active);}

    void setContext(IdleContext c);
    void keepAwake(IdleLease owner,uint32_t milliseconds); // Expiring native lease; no fake touch / CPU burst.

    IdleStage stage() const {
        return static_cast<IdleStage>(
            stage_.load(std::memory_order_acquire));
    }

    IdleProfile profile() const;

private:
    std::atomic<uint32_t> lastActivity_{0};
    std::atomic<bool> idleActionActive_{false};
    std::atomic<uint32_t> activityGeneration_{0};
    std::atomic<uint8_t> stage_{
        static_cast<uint8_t>(IdleStage::Active)
    };

    // Context changes are infrequent, but can originate outside the task that
    // runs loop(). Store it atomically for the same reason as stage_.
    std::atomic<uint8_t> ctx_{
        static_cast<uint8_t>(IdleContext::Normal)
    };

    std::atomic<uint32_t> leases_[static_cast<size_t>(IdleLease::Count)]{};
    bool awakeLease(uint32_t now)const;
    void change(IdleStage s);
};

} // namespace hp
