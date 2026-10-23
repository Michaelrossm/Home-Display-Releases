#include "NetworkWorker.h"
#include "../core/SystemSupervisor.h"
#include "../core/EventLog.h"
#include "../core/PerformancePolicy.h"
namespace hp {
namespace {
void updateMaximum(std::atomic<uint32_t>& maximum,uint32_t value){
 auto old=maximum.load(std::memory_order_relaxed);
 while(value>old&&!maximum.compare_exchange_weak(old,value,std::memory_order_relaxed)){}
}
}
NetworkWorker& NetworkWorker::instance(){static NetworkWorker worker;return worker;}
bool NetworkWorker::begin(){
 if(task_)return true;
 q_=xQueueCreate(8,sizeof(Job));priorityQ_=xQueueCreate(4,sizeof(Job));transferQ_=xQueueCreate(2,sizeof(Job));
 if(!q_||!priorityQ_||!transferQ_){
    if(q_)vQueueDelete(q_);if(priorityQ_)vQueueDelete(priorityQ_);if(transferQ_)vQueueDelete(transferQ_);
    q_=priorityQ_=transferQ_=nullptr;return false;
 }
 if(xTaskCreatePinnedToCore(entry,"hd-net",12288,this,2,&task_,
      performance::kIoCore<0?tskNO_AFFINITY:performance::kIoCore)!=pdPASS){
    vQueueDelete(q_);vQueueDelete(priorityQ_);vQueueDelete(transferQ_);
    q_=priorityQ_=transferQ_=nullptr;task_=nullptr;return false;
 }
 // A single, bounded transfer worker; no uncontrolled per-request tasks.
 if(xTaskCreatePinnedToCore(transferEntry,"hd-transfer",16384,this,2,&transferTask_,
      performance::kIoCore<0?tskNO_AFFINITY:performance::kIoCore)!=pdPASS){
    transferTask_=nullptr;vQueueDelete(transferQ_);transferQ_=nullptr;
    EventLog::instance().add("WARN","NET_TRANSFER_WORKER","Fallback to normal queue");
 }
 return true;
}
bool NetworkWorker::enqueue(Fn fn,void* ctx,const char* tag,bool priority,bool network){
 if(!fn||!task_||!q_||!priorityQ_){++failedSubmit_;return false;}
 Job job{};job.fn=fn;job.ctx=ctx;job.queuedAt=millis();strlcpy(job.tag,tag?tag:"net",sizeof(job.tag));
 QueueHandle_t queue=priority?priorityQ_:q_;
 job.network=network;if(network)networkQueued_.fetch_add(1,std::memory_order_relaxed);
 if(xQueueSend(queue,&job,0)!=pdTRUE){
     if(!priority||xQueueSend(q_,&job,0)!=pdTRUE){
         if(network)networkQueued_.fetch_sub(1,std::memory_order_relaxed);
         ++failedSubmit_;EventLog::instance().add("WARN","NET_QUEUE_FULL",job.tag);return false;
     }
 }
 updateMaximum(queuePeak_,pending());
 xTaskNotifyGive(task_);
 return true;
}
bool NetworkWorker::submit(Fn fn,void* ctx,const char* tag){return enqueue(fn,ctx,tag,false);}
bool NetworkWorker::submitPriority(Fn fn,void* ctx,const char* tag){return enqueue(fn,ctx,tag,true);}
bool NetworkWorker::submitLocal(Fn fn,void* ctx,const char* tag,bool priority){return enqueue(fn,ctx,tag,priority,false);}
bool NetworkWorker::submitLocalTransfer(Fn fn,void* ctx,const char* tag){return enqueueTransfer(fn,ctx,tag,false);}
bool NetworkWorker::submitTransfer(Fn fn,void* ctx,const char* tag){
 return enqueueTransfer(fn,ctx,tag,true);
}
bool NetworkWorker::enqueueTransfer(Fn fn,void* ctx,const char* tag,bool network){
 if(!fn){++failedSubmit_;return false;}
 if(!transferQ_||!transferTask_)return enqueue(fn,ctx,tag,false,network);
 Job job{};job.fn=fn;job.ctx=ctx;job.queuedAt=millis();strlcpy(job.tag,tag?tag:"transfer",sizeof(job.tag));
 job.network=network;if(network)networkQueued_.fetch_add(1,std::memory_order_relaxed);
 if(xQueueSend(transferQ_,&job,0)!=pdTRUE){if(network)networkQueued_.fetch_sub(1,std::memory_order_relaxed);++failedSubmit_;EventLog::instance().add("WARN","NET_TRANSFER_QUEUE_FULL",job.tag);return false;}
 updateMaximum(queuePeak_,pending());
 return true;
}
void NetworkWorker::entry(void* ctx){
 Serial.printf("[PERF] NetworkWorker core=%d\n",static_cast<int>(xPortGetCoreID()));
 static_cast<NetworkWorker*>(ctx)->run();
}
void NetworkWorker::transferEntry(void* ctx){
 Serial.printf("[PERF] TransferWorker core=%d\n",static_cast<int>(xPortGetCoreID()));
 static_cast<NetworkWorker*>(ctx)->runTransfers();
}
void NetworkWorker::noteNetworkActivity(){lastNetworkMs_.store(millis(),std::memory_order_relaxed);networkSeen_.store(true,std::memory_order_release);}
void NetworkWorker::beginNetworkActivity(){networkTransfers_.fetch_add(1,std::memory_order_relaxed);noteNetworkActivity();}
void NetworkWorker::endNetworkActivity(){noteNetworkActivity();networkTransfers_.fetch_sub(1,std::memory_order_release);}
bool NetworkWorker::radioBurstActive()const{
 return performance::radioBurst(networkTransfers_.load(std::memory_order_acquire),
    networkQueued_.load(std::memory_order_acquire),networkSeen_.load(std::memory_order_acquire),
    lastNetworkMs_.load(std::memory_order_relaxed),millis());
}
bool NetworkWorker::addMaintenance(Fn fn,void* ctx){
 if(!fn)return false;bool ok=false;portENTER_CRITICAL(&maintenanceMux_);
 for(auto& hook:maintenance_)if(hook.fn==fn&&hook.ctx==ctx){ok=true;break;}
 if(!ok)for(auto& hook:maintenance_)if(!hook.fn){hook={fn,ctx};ok=true;break;}
 portEXIT_CRITICAL(&maintenanceMux_);return ok;
}
void NetworkWorker::maintain(){
 if(xTaskGetCurrentTaskHandle()!=task_||maintaining_)return;
 maintaining_=true;Maintenance hooks[8];
 portENTER_CRITICAL(&maintenanceMux_);for(size_t i=0;i<8;++i)hooks[i]=maintenance_[i];portEXIT_CRITICAL(&maintenanceMux_);
 for(const auto& hook:hooks){
     // Do not start another blocking background phase ahead of an accepted
     // user action. A currently running synchronous request is not preempted.
     if(priorityDepth())break;
     if(hook.fn)hook.fn(hook.ctx);
 }
 SystemSupervisor::instance().heartbeat(ServiceId::Network);maintaining_=false;
}
void NetworkWorker::executeJob(const Job& job,bool transfer){
 const uint32_t waited=millis()-job.queuedAt;updateMaximum(maxWaitMs_,waited);
 waitLatency_[transfer?1:0].add(waited);
 if(!transfer)SystemSupervisor::instance().heartbeat(ServiceId::Network);
 if(transfer)transferRunning_.store(true,std::memory_order_release);
 else processing_.store(true,std::memory_order_release);
 if(job.network){beginNetworkActivity();networkQueued_.fetch_sub(1,std::memory_order_relaxed);}
 const uint32_t started=millis();
 job.fn(job.ctx);
 const uint32_t elapsed=millis()-started;
 updateMaximum(maxJobMs_,elapsed);
 jobLatency_[transfer?1:0].add(elapsed);
 if(elapsed>3000)longJobs_.fetch_add(1,std::memory_order_relaxed);
 if(job.network)endNetworkActivity();
 if(transfer){transferRunning_.store(false,std::memory_order_release);++completedTransfers_;}
 else processing_.store(false,std::memory_order_release);
 ++completed_;
}
void NetworkWorker::run(){
 Job job{};unsigned priorityStreak=0;
 for(;;){
  bool selected=false;
  if(priorityStreak<3||uxQueueMessagesWaiting(q_)==0)
      if(xQueueReceive(priorityQ_,&job,0)==pdTRUE){selected=true;++priorityStreak;}
  if(!selected&&xQueueReceive(q_,&job,0)==pdTRUE){selected=true;priorityStreak=0;}
  if(!selected&&xQueueReceive(priorityQ_,&job,0)==pdTRUE){selected=true;++priorityStreak;}
  if(selected)executeJob(job,false);
  maintain();SystemSupervisor::instance().heartbeat(ServiceId::Network);
  // Enqueue wakes this existing task immediately. Notifications are counting:
  // a send between the empty check and sleep is not lost. Periodic maintenance
  // still gets a bounded idle opportunity without polling both queues.
  if(!uxQueueMessagesWaiting(q_)&&!priorityDepth())ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
 }
}
void NetworkWorker::runTransfers(){
 Job job{};
 for(;;)if(xQueueReceive(transferQ_,&job,portMAX_DELAY)==pdTRUE)executeJob(job,true);
}
}
