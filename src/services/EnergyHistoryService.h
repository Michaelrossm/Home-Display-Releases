#pragma once
#include "EnergyModel.h"
#include "EnergyDetailArchive.h"
#include "../core/BackgroundScheduler.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace fs {class FS;}
namespace hp {
class EnergyHistoryService : public IBackgroundJob {
public:
    static EnergyHistoryService& instance();
    bool begin();
    void backgroundTick() override;
    BackgroundJobClass backgroundClass()const override{return BackgroundJobClass::Blocking;}
    uint32_t backgroundIntervalMs()const override{return 60000;}
    const char* backgroundName()const override{return "energy-history";}
    void setHistoryProvider(EnergyHistoryProvider* provider){historyProvider_=provider;}
    bool tryDetailStatus(EnergyDetailStatus& out);
    bool visitDetail(uint32_t from,uint32_t to,const std::function<bool(const DetailRecord&)>& visitor);
    void record(const EnergySnapshot& snapshot); // Existing worker only.
    String query(const String& period,size_t maxPoints=256); // Worker only.
private:
    EnergyHistoryService() : mutex_(xSemaphoreCreateMutex()) {}
    struct Point { int64_t timestamp=0; float values[7]={NAN,NAN,NAN,NAN,NAN,NAN,NAN}; uint16_t samples[5]{}; };
    struct State {
        uint32_t magic=0x454e4832,version=2,checksum=0;
        uint16_t capacity[4]{},count[4]{},head[4]{};
    };
    bool initialize();
    void aggregate(unsigned tier,const Point& point,int64_t bucket);
    void persist();
    bool validFile(fs::FS& fs,const String& path);
    State state_;
    Point* points_=nullptr;
    uint32_t lastPersist_=0;
    int64_t lastMinute_=0;
    SemaphoreHandle_t mutex_=nullptr;
    SemaphoreHandle_t statusMutex_=nullptr;
    EnergyDetailArchive* detail_=nullptr;
    EnergyDetailStatus detailStatus_;
    EnergyHistoryProvider* historyProvider_=nullptr;
    void updateDetailStatus();
};
}
