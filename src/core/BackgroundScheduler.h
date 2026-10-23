#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>

namespace hp {

enum class BackgroundJobClass : uint8_t {
    Quick,      // Must return quickly; executed by the lightweight scheduler.
    Blocking    // Network/file work; executed by the worker pool.
};

class IBackgroundJob {
public:
    virtual ~IBackgroundJob() = default;
    virtual void backgroundTick() = 0;
    virtual BackgroundJobClass backgroundClass() const {
        return BackgroundJobClass::Quick;
    }
    virtual uint32_t backgroundIntervalMs() const { return 1000; }
    virtual const char* backgroundName() const { return "background"; }
};

class BackgroundScheduler {
public:
    static BackgroundScheduler& instance();

    bool add(IBackgroundJob* j);
    bool begin();

    UBaseType_t stackHighWater() const {
        return schedulerTask_ ? uxTaskGetStackHighWaterMark(schedulerTask_) : 0;
    }
    UBaseType_t workerStackHighWater() const;
    uint32_t completedQuick() const { return completedQuick_; }
    uint32_t completedBlocking() const { return completedBlocking_; }
    uint32_t queueRejects() const { return queueRejects_; }
    uint32_t maxQuickUs() const { return maxQuickUs_; }
    uint32_t maxBlockingUs() const { return maxBlockingUs_; }

private:
    static constexpr size_t MAX = 10;
    static constexpr uint8_t WORKER_COUNT = 1;

    struct WorkItem {
        uint8_t index = 0;
    };

    IBackgroundJob* jobs_[MAX]{};
    bool inFlight_[MAX]{};
    bool scheduled_[MAX]{};
    uint32_t nextRunMs_[MAX]{};
    size_t n_ = 0;

    TaskHandle_t schedulerTask_ = nullptr;
    TaskHandle_t workerTasks_[WORKER_COUNT]{};
    QueueHandle_t blockingQueue_ = nullptr;
    SemaphoreHandle_t stateLock_ = nullptr;

    std::atomic<uint32_t> completedQuick_{0};
    std::atomic<uint32_t> completedBlocking_{0};
    std::atomic<uint32_t> queueRejects_{0};
    std::atomic<uint32_t> maxQuickUs_{0};
    std::atomic<uint32_t> maxBlockingUs_{0};

    static void schedulerEntry(void* p);
    static void workerEntry(void* p);
    void runScheduler();
    void runWorker();
    bool scheduleBlocking(uint8_t index);
    void clearInFlight(uint8_t index);
    void updateMax(std::atomic<uint32_t>& target, uint32_t value);
};

}
