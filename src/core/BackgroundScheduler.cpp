#include "BackgroundScheduler.h"
#include "../services/IdleService.h"
#include "../core/EventLog.h"
#include "PerformancePolicy.h"
#include <esp_timer.h>
#include <algorithm>
#include "AppBackgroundScheduler.h"
#include "../services/NetworkService.h"
#include "../services/SecurityService.h"
#include "../services/NetworkWorker.h"

namespace hp {

AppBackgroundScheduler& AppBackgroundScheduler::instance(){static AppBackgroundScheduler s;return s;}
bool AppBackgroundScheduler::begin(){if(!mutex_)mutex_=xSemaphoreCreateMutex();return mutex_&&BackgroundScheduler::instance().add(this);}
bool AppBackgroundScheduler::store(const char* owner,const AppBackgroundPolicy& input,Handler handler,bool approved){
    if(!owner||!owner[0]||strlen(owner)>31||!validBackgroundPolicy(input)||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;
    for(const char* p=owner;*p;++p)if(!(*p>='a'&&*p<='z')&&!(*p>='0'&&*p<='9')&&*p!='_'&&*p!='-'){xSemaphoreGive(mutex_);return false;}
    Entry* target=nullptr;for(auto& e:entries_)if(!strcmp(e.owner,owner)){target=&e;break;}if(!target)for(auto& e:entries_)if(!e.owner[0]){target=&e;break;}
    if(!target){xSemaphoreGive(mutex_);return false;}
    if(target->handler&&!handler){xSemaphoreGive(mutex_);return false;} // Lua interest cannot replace approved firmware work.
    if(input.priority==AppBackgroundPriority::None){*target=Entry{};if(pendingBusy_&&!strcmp(pending_.owner,owner)){pendingCancelled_=true;if(luaPending_.exchange(false)){pendingBusy_=false;pending_=Entry{};}}}
    else {const bool fresh=!target->owner[0];strlcpy(target->owner,owner,sizeof(target->owner));target->policy=appPolicy(input,approved);target->handler=handler;if(fresh)target->due=millis()+target->policy.preferredIntervalMs;}
    diagnostics_.policies=0;for(const auto& e:entries_)if(e.owner[0])++diagnostics_.policies;xSemaphoreGive(mutex_);return true;
}
bool AppBackgroundScheduler::request(const char* owner,const AppBackgroundPolicy& policy){return store(owner,policy,nullptr,false);}
bool AppBackgroundScheduler::registerNative(const char* owner,const AppBackgroundPolicy& policy,Handler handler,bool approved){return handler&&store(owner,policy,handler,approved);}
void AppBackgroundScheduler::remove(const char* owner){if(!owner||!mutex_||xSemaphoreTake(mutex_,portMAX_DELAY)!=pdTRUE)return;for(auto& e:entries_)if(!strcmp(e.owner,owner))e=Entry{};if(pendingBusy_&&!strcmp(pending_.owner,owner)){pendingCancelled_=true;if(luaPending_.exchange(false)){pendingBusy_=false;pending_=Entry{};}}diagnostics_.policies=0;for(const auto& e:entries_)if(e.owner[0])++diagnostics_.policies;xSemaphoreGive(mutex_);}
void AppBackgroundScheduler::setForeground(const char* owner){load_=ForegroundLoad::Normal;if(!mutex_||xSemaphoreTake(mutex_,portMAX_DELAY)!=pdTRUE)return;strlcpy(foreground_,owner?owner:"",sizeof(foreground_));xSemaphoreGive(mutex_);}
bool AppBackgroundScheduler::isForeground(const char* owner){
    if(!owner||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;
    const bool result=!strcmp(foreground_,owner);xSemaphoreGive(mutex_);return result;
}
bool AppBackgroundScheduler::tryDiagnostics(Diagnostics& out){if(!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;out=diagnostics_;xSemaphoreGive(mutex_);return true;}
void AppBackgroundScheduler::backgroundTick(){
    if(SecurityService::instance().tabletLocked()||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return;
    if(pendingBusy_){xSemaphoreGive(mutex_);return;}
    const uint32_t now=millis();const bool online=NetworkService::instance().online();const auto load=load_.load();Entry* selected=nullptr;
    for(auto& e:entries_){if(!e.owner[0]||!strcmp(e.owner,foreground_)||static_cast<int32_t>(now-e.due)<0)continue;
        if(e.policy.requiresNetwork&&!online){++diagnostics_.deferredOffline;continue;}
        if(!backgroundAllowed(e.policy,load,online)){++diagnostics_.deferredHigh;continue;}
        const bool urgent=static_cast<uint32_t>(now-e.due)>=e.policy.maximumDelayMs-e.policy.preferredIntervalMs;
        const bool selectedUrgent=selected&&static_cast<uint32_t>(now-selected->due)>=selected->policy.maximumDelayMs-selected->policy.preferredIntervalMs;
        if(!selected||(urgent&&!selectedUrgent)||(urgent==selectedUrgent&&(e.policy.priority>selected->policy.priority||(e.policy.priority==selected->policy.priority&&static_cast<int32_t>(e.due-selected->due)<0))))selected=&e;
    }
    if(!selected){xSemaphoreGive(mutex_);return;}
    pending_=*selected;pendingBusy_=true;pendingCancelled_=false;
    if(!pending_.handler){selected->due=now+selected->policy.preferredIntervalMs;luaPending_.store(true,std::memory_order_release);xSemaphoreGive(mutex_);return;}
    if(pending_.policy.requiresNetwork&&!NetworkWorker::instance().submit(executePending,this,"app-background")){pendingBusy_=false;xSemaphoreGive(mutex_);return;}
    selected->due=now+selected->policy.preferredIntervalMs;const bool network=pending_.policy.requiresNetwork;xSemaphoreGive(mutex_);
    if(!network)executePending(this);
}
bool AppBackgroundScheduler::ownerAllowed(const char* owner){if(!owner||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;
    bool allowed=false;for(const auto& e:entries_)if(!strcmp(e.owner,owner)&&e.owner[0]){allowed=backgroundAllowed(e.policy,load_.load(),NetworkService::instance().online());break;}
    xSemaphoreGive(mutex_);return allowed&&!SecurityService::instance().tabletLocked();
}
bool AppBackgroundScheduler::hasPolicy(const char* owner){if(!owner||!mutex_)return false;
    // An unknown (contended) lookup is not proof of removal. ownerAllowed will
    // separately refuse execution and the UI keeps this completion for retry.
    if(xSemaphoreTake(mutex_,0)!=pdTRUE)return true;bool found=false;
    for(const auto& e:entries_)if(e.owner[0]&&!strcmp(e.owner,owner)){found=true;break;}xSemaphoreGive(mutex_);return found;
}
bool AppBackgroundScheduler::takeLuaOwner(char (&owner)[32]){if(!luaPending()||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return false;
    bool valid=pendingBusy_&&!pending_.handler&&!pendingCancelled_&&!SecurityService::instance().tabletLocked()&&strcmp(pending_.owner,foreground_)&&backgroundAllowed(pending_.policy,load_.load(),NetworkService::instance().online());
    luaPending_=false;
    if(valid)strlcpy(owner,pending_.owner,sizeof(owner));
    else {for(auto& e:entries_)if(e.owner[0]&&!strcmp(e.owner,pending_.owner))e.due=pending_.due;pendingBusy_=false;pending_=Entry{};}
    xSemaphoreGive(mutex_);return valid;
}
void AppBackgroundScheduler::finishLua(uint32_t elapsedUs){if(!mutex_||xSemaphoreTake(mutex_,portMAX_DELAY)!=pdTRUE)return;
    if(pendingBusy_&&!pending_.handler){++diagnostics_.executed;diagnostics_.maxCallbackUs=std::max(diagnostics_.maxCallbackUs,elapsedUs);pendingBusy_=false;pending_=Entry{};}
    xSemaphoreGive(mutex_);
}
void AppBackgroundScheduler::recordLuaEvent(uint32_t elapsedUs){if(!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return;
    ++diagnostics_.executed;diagnostics_.maxCallbackUs=std::max(diagnostics_.maxCallbackUs,elapsedUs);xSemaphoreGive(mutex_);
}
void AppBackgroundScheduler::executePending(void* context){auto& service=*static_cast<AppBackgroundScheduler*>(context);xSemaphoreTake(service.mutex_,portMAX_DELAY);Entry job=service.pending_;
    const bool cancelled=service.pendingCancelled_||SecurityService::instance().tabletLocked()||!strcmp(job.owner,service.foreground_)||!backgroundAllowed(job.policy,service.load_.load(),NetworkService::instance().online());
    xSemaphoreGive(service.mutex_);uint32_t elapsed=0;if(!cancelled){const int64_t started=esp_timer_get_time();job.handler(job.owner);elapsed=std::min<int64_t>(UINT32_MAX,esp_timer_get_time()-started);}
    xSemaphoreTake(service.mutex_,portMAX_DELAY);if(!cancelled){++service.diagnostics_.executed;service.diagnostics_.maxCallbackUs=std::max(service.diagnostics_.maxCallbackUs,elapsed);}else for(auto& e:service.entries_)if(!strcmp(e.owner,job.owner))e.due=job.due;
    service.pendingBusy_=false;service.pending_=Entry{};xSemaphoreGive(service.mutex_);
}

BackgroundScheduler& BackgroundScheduler::instance() {
    static BackgroundScheduler s;
    return s;
}

bool BackgroundScheduler::add(IBackgroundJob* j) {
    if (!j || schedulerTask_ || n_ >= MAX) return false;

    for (size_t i = 0; i < n_; ++i) {
        if (jobs_[i] == j) return true;
    }

    jobs_[n_] = j;
    scheduled_[n_] = false;
    nextRunMs_[n_] = 0;
    ++n_;
    return true;
}

bool BackgroundScheduler::begin() {
    if (schedulerTask_) return true;
    if (n_ == 0) return true;

    stateLock_ = xSemaphoreCreateMutex();
    blockingQueue_ = xQueueCreate(MAX, sizeof(WorkItem));
    auto cleanup = [&]() {
        // No jobs are submitted until the scheduler task exists.
        for (auto& worker : workerTasks_) {
            if (worker) vTaskDelete(worker);
            worker = nullptr;
        }
        if (blockingQueue_) vQueueDelete(blockingQueue_);
        if (stateLock_) vSemaphoreDelete(stateLock_);
        blockingQueue_ = nullptr;
        stateLock_ = nullptr;
    };
    if (!stateLock_ || !blockingQueue_) { cleanup(); return false; }

    // One blocking worker keeps slow jobs away from the quick scheduler,
    // but serializes HTTP/FSAPI/file access. This avoids concurrent access to
    // ESP-Hosted/network clients from multiple background jobs.
    for (uint8_t i = 0; i < WORKER_COUNT; ++i) {
        String name = String("hp-bgw") + String(i);
        if (xTaskCreatePinnedToCore(
                // Power polling also publishes energy snapshots and persists history.
                // Its nested storage/time calls exceed the former 8 KiB stack.
                workerEntry, name.c_str(), 16384, this, 1,
                &workerTasks_[i], performance::kIoCore<0 ? tskNO_AFFINITY : performance::kIoCore) != pdPASS) {
            EventLog::instance().add(
                "ERROR", "BG_WORKER_START",
                String("Worker ") + String(i) + " konnte nicht gestartet werden"
            );
            cleanup();
            return false;
        }
    }

    if (xTaskCreate(
            schedulerEntry, "hp-bg", 6144, this, 1,
            &schedulerTask_) != pdPASS) {
        EventLog::instance().add(
            "ERROR", "BG_SCHED_START",
            "Background-Scheduler konnte nicht gestartet werden"
        );
        cleanup();
        return false;
    }

    EventLog::instance().add(
        "INFO", "BG_SCHED_READY",
        String("Background-Scheduler: Quick-Lane + ") + String(WORKER_COUNT) + " Blocking-Worker aktiv"
    );
    return true;
}

void BackgroundScheduler::schedulerEntry(void* p) {
    static_cast<BackgroundScheduler*>(p)->runScheduler();
}

void BackgroundScheduler::workerEntry(void* p) {
    static_cast<BackgroundScheduler*>(p)->runWorker();
}

void BackgroundScheduler::updateMax(
    std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t previous = target.load(std::memory_order_relaxed);
    while (value > previous && !target.compare_exchange_weak(
        previous, value, std::memory_order_relaxed)) {}
}

bool BackgroundScheduler::scheduleBlocking(uint8_t index) {
    if (!blockingQueue_ || index >= n_) return false;

    const uint32_t now = millis();
    if (scheduled_[index] &&
        static_cast<int32_t>(now - nextRunMs_[index]) < 0) return true;

    if (xSemaphoreTake(stateLock_, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;

    if (inFlight_[index]) {
        xSemaphoreGive(stateLock_);
        return true;
    }
    inFlight_[index] = true;
    xSemaphoreGive(stateLock_);

    WorkItem item{};
    item.index = index;
    if (xQueueSend(blockingQueue_, &item, 0) == pdTRUE) {
        const uint32_t interval = jobs_[index]->backgroundIntervalMs();
        nextRunMs_[index] = now + (interval < 200UL ? 200UL : interval);
        scheduled_[index] = true;
        return true;
    }

    clearInFlight(index);
    ++queueRejects_;
    EventLog::instance().add(
        "WARN", "BG_QUEUE_FULL",
        jobs_[index] ? jobs_[index]->backgroundName() : "unknown"
    );
    return false;
}

void BackgroundScheduler::clearInFlight(uint8_t index) {
    if (!stateLock_) return;
    // The lock only protects short flag updates. Never abandon a completed
    // job's flag: doing so would permanently suppress its future execution.
    if (xSemaphoreTake(stateLock_, portMAX_DELAY) == pdTRUE) {
        if (index < MAX) inFlight_[index] = false;
        xSemaphoreGive(stateLock_);
    }
}

void BackgroundScheduler::runScheduler() {
    for (;;) {
        for (uint8_t i = 0; i < n_; ++i) {
            IBackgroundJob* job = jobs_[i];
            if (!job) continue;

            if (job->backgroundClass() == BackgroundJobClass::Blocking) {
                scheduleBlocking(i);
                continue;
            }

            const uint32_t now = millis();
            if (scheduled_[i] &&
                static_cast<int32_t>(now - nextRunMs_[i]) < 0) continue;
            const uint32_t interval = job->backgroundIntervalMs();
            nextRunMs_[i] = now + (interval < 200UL ? 200UL : interval);
            scheduled_[i] = true;

            const int64_t started = esp_timer_get_time();
            job->backgroundTick();
            const uint32_t elapsed =
                static_cast<uint32_t>(esp_timer_get_time() - started);
            updateMax(maxQuickUs_, elapsed);
            ++completedQuick_;

            // Keep quick callbacks cooperative with the rest of the firmware.
            taskYIELD();
        }

        uint32_t pauseMs = 200;
        switch (IdleService::instance().stage()) {
            case IdleStage::Active:  pauseMs = 200; break;
            case IdleStage::Dimmed:  pauseMs = 500; break;
            case IdleStage::Saver:   pauseMs = 1000; break;
            case IdleStage::Off:     pauseMs = 2000; break;
        }
        vTaskDelay(pdMS_TO_TICKS(pauseMs));
    }
}

void BackgroundScheduler::runWorker() {
    WorkItem item{};
    for (;;) {
        if (xQueueReceive(
                blockingQueue_, &item, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        IBackgroundJob* job =
            item.index < n_ ? jobs_[item.index] : nullptr;

        if (job) {
            const int64_t started = esp_timer_get_time();
            job->backgroundTick();
            const uint32_t elapsed =
                static_cast<uint32_t>(esp_timer_get_time() - started);
            updateMax(maxBlockingUs_, elapsed);
            ++completedBlocking_;

            // Very slow jobs are visible in diagnostics instead of silently
            // starving the scheduler.
            if (elapsed >= 2000000UL) {
                // Never enqueue persistence from persistence diagnostics:
                // a slow event-log write would otherwise generate another
                // durable record on every turn and perpetuate flash traffic.
                EventLog::instance().addTransient(
                    "WARN", "BG_JOB_SLOW",
                    String(job->backgroundName()) +
                    " brauchte " + String(elapsed / 1000UL) + " ms"
                );
            }
        }

        clearInFlight(item.index);
        // A yield only serves equal/higher-priority runnable tasks. A busy
        // queue must also give the idle task a turn (task-watchdog coverage).
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

UBaseType_t BackgroundScheduler::workerStackHighWater() const {
    UBaseType_t minimum = 0;
    for (uint8_t i = 0; i < WORKER_COUNT; ++i) {
        if (!workerTasks_[i]) continue;
        const UBaseType_t value =
            uxTaskGetStackHighWaterMark(workerTasks_[i]);
        if (!minimum || value < minimum) minimum = value;
    }
    return minimum;
}

}
