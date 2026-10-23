#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include "../core/LatencyHistogram.h"
namespace hp {
class NetworkWorker {
public:
 using Fn=void(*)(void*);
 static NetworkWorker& instance();
 bool begin();
 bool submit(Fn,void*,const char* tag=nullptr);
 bool submitPriority(Fn,void*,const char* tag=nullptr);
 // Local storage work shares existing tasks but never requests a radio burst.
 bool submitLocal(Fn,void*,const char* tag=nullptr,bool priority=false);
 bool submitLocalTransfer(Fn,void*,const char* tag=nullptr);
 // Long file transfers and directory scans cannot stall status polling.
 bool submitTransfer(Fn,void*,const char* tag=nullptr);
 bool addMaintenance(Fn,void*);
 void maintain();
 bool isWorkerContext()const {
     auto* current=xTaskGetCurrentTaskHandle();
     return current&&(current==task_||current==transferTask_);
 }
 bool backgroundWindow()const {
     return task_&&xTaskGetCurrentTaskHandle()==task_&&!processing_&&q_&&priorityDepth()==0;
 }
 bool queueEmpty()const{return pending()==0&&!transferRunning_.load(std::memory_order_acquire);}
 UBaseType_t pending()const{
     return (q_?uxQueueMessagesWaiting(q_):0)+(priorityQ_?uxQueueMessagesWaiting(priorityQ_):0)+
            (transferQ_?uxQueueMessagesWaiting(transferQ_):0);
 }
 UBaseType_t priorityDepth()const{return priorityQ_?uxQueueMessagesWaiting(priorityQ_):0;}
 void noteNetworkActivity();
 void beginNetworkActivity();
 void endNetworkActivity();
 bool radioBurstActive()const;
 UBaseType_t stackHighWater()const{return task_?uxTaskGetStackHighWaterMark(task_):0;}
 UBaseType_t transferStackHighWater()const{return transferTask_?uxTaskGetStackHighWaterMark(transferTask_):0;}
 uint32_t completed()const{return completed_.load(std::memory_order_relaxed);}
 uint32_t failedSubmit()const{return failedSubmit_.load(std::memory_order_relaxed);}
 uint32_t maxWaitMs()const{return maxWaitMs_.load(std::memory_order_relaxed);}
 uint32_t maxJobMs()const{return maxJobMs_.load(std::memory_order_relaxed);}
 uint32_t longJobs()const{return longJobs_.load(std::memory_order_relaxed);}
 uint32_t completedTransfers()const{return completedTransfers_.load(std::memory_order_relaxed);}
 LatencyHistogram::Snapshot waitLatency(bool transfer=false)const{return waitLatency_[transfer?1:0].snapshot();}
 LatencyHistogram::Snapshot jobLatency(bool transfer=false)const{return jobLatency_[transfer?1:0].snapshot();}
 uint32_t queuePeak()const{return queuePeak_.load(std::memory_order_relaxed);}
private:
 struct Job{Fn fn=nullptr;void* ctx=nullptr;uint32_t queuedAt=0;char tag[16]{};bool network=true;};
 struct Maintenance{Fn fn=nullptr;void* ctx=nullptr;};
 bool enqueue(Fn,void*,const char*,bool priority,bool network=true);
 bool enqueueTransfer(Fn,void*,const char*,bool network);
 static void entry(void*);
 static void transferEntry(void*);
 void run();
 void runTransfers();
 void executeJob(const Job&,bool transfer);
 Maintenance maintenance_[8]{};
 portMUX_TYPE maintenanceMux_=portMUX_INITIALIZER_UNLOCKED;
 bool maintaining_=false;
 std::atomic<bool> processing_{false},transferRunning_{false};
 std::atomic<unsigned> networkTransfers_{0};
 std::atomic<unsigned> networkQueued_{0};
 std::atomic<bool> networkSeen_{false};
 std::atomic<uint32_t> lastNetworkMs_{0};
 QueueHandle_t q_=nullptr,priorityQ_=nullptr,transferQ_=nullptr;
 TaskHandle_t task_=nullptr,transferTask_=nullptr;
 std::atomic<uint32_t> completed_{0},failedSubmit_{0},maxWaitMs_{0},maxJobMs_{0},longJobs_{0},completedTransfers_{0};
 LatencyHistogram waitLatency_[2],jobLatency_[2];
 std::atomic<uint32_t> queuePeak_{0};
};
}
