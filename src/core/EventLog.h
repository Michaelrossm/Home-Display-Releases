#pragma once
#include <Arduino.h>
#include <functional>
#include "BackgroundScheduler.h"
namespace hp {
class EventLog : public IBackgroundJob {
public:
 struct __attribute__((packed)) Record{uint32_t timestamp;uint32_t uptimeSeconds;char level[8];char code[24];char message[120];};
 using Callback=std::function<bool(const Record&)>;
 static EventLog& instance();
 bool begin(bool persistent=true); bool setPersistence(bool); bool add(const char*level,const char*code,const String&message); bool forEach(const Callback&); bool clear();
 // Boot writes are synchronous until the existing background worker starts.
 // Runtime durable records are queued; transient records stay RAM-only.
 bool addTransient(const char*level,const char*code,const String&message);
 bool deferPersistence();
 void backgroundTick() override;
 BackgroundJobClass backgroundClass() const override{return BackgroundJobClass::Blocking;}
 uint32_t backgroundIntervalMs() const override{return 250;}
 const char* backgroundName() const override{return "event-log";}
 uint32_t persistenceDrops()const{return persistenceDrops_.load();}
 uint32_t persistenceFailures()const{return persistenceFailures_.load();}
 String recentText(size_t maxItems=6, bool warningsAndErrorsOnly=true);
 uint32_t warningCount(); uint32_t errorCount();
 size_t count()const; bool persistent()const{return persistent_;}
private:
 struct __attribute__((packed)) Header{uint32_t magic,writeIndex,count,generation,checksum;};
 static constexpr uint32_t kMagic=0x48444531; // HDE1
 static constexpr uint32_t kCapacity=512;
 Header fileHeader_{}; Record*records_=nullptr;uint32_t writeIndex_=0,count_=0;std::atomic<bool> ready_{false},persistent_{false};
 mutable portMUX_TYPE ringMux_=portMUX_INITIALIZER_UNLOCKED;
 std::atomic<QueueHandle_t> pending_{nullptr};SemaphoreHandle_t fileLock_=nullptr;
 std::atomic<uint32_t> persistenceDrops_{0},persistenceFailures_{0};
 uint32_t checksum(const Header&)const;bool valid(const Header&)const;bool validRecord(const Record&)const;void appendRam(const Record&);bool initializeFile();bool loadPersistent();bool writePersistent(const Record&);
 bool record(const char*level,const char*code,const String&message,bool durable);
};
}
