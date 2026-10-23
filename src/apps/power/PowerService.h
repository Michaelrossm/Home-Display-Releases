#pragma once
#include "../../core/BackgroundScheduler.h"
#include "engine/PowerMeterClient.h"
#include "PowerDiscovery.h"
#include <freertos/semphr.h>
#include <atomic>
namespace hp { class PowerService:public IBackgroundJob,public EnergyHistoryProvider { public: static PowerService& instance(); bool begin(); void backgroundTick()override;
 bool supportsHistoryBackfill()const override;
 const char* historySource()const override{return "IR-Tracker";}
 bool fetchHistory(int64_t from,int64_t to,uint16_t resolution,const Visitor& visitor)override;
 bool bindSource(const String& owner,unsigned index); // Existing worker only.
 void cleanupOwner(const char* owner,bool suspending); // Nonblocking cancellation only.
 private:
 String sourceOwner_;unsigned sourceIndex_=0;std::atomic<uint32_t> sourceOwnerHash_{0};std::atomic<bool> sourceCancelled_{false},sourceChanged_{false};
 bool source(String& host,String& password);
 uint32_t pollIntervalMs_=3000; // Blocking worker-owned offline backoff.
 public:
 BackgroundJobClass backgroundClass() const override { return BackgroundJobClass::Blocking; }
 const char* backgroundName() const override { return "power"; } PowerMeterData snapshot(); PowerMeterHistoryView history(); bool trySnapshot(PowerMeterData& out); bool tryHistory(PowerMeterHistoryView& out); bool requestDiscovery(); int copyDiscovered(PowerDiscoveryDevice*,int); int tryCopyDiscovered(PowerDiscoveryDevice*,int); String discoveryStatus(); bool tryDiscoveryStatus(String&); private: static void discoveryJob(void*); void executeDiscovery(); PowerMeterClient client_; PowerMeterData data_; PowerMeterHistoryView hist_; PowerDiscoveryDevice discovered_[8]; SemaphoreHandle_t mux_=nullptr; uint32_t last_=0,lastHist_=0; bool discoveryActive_=false; String discoveryStatus_; }; }
