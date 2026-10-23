#pragma once
#include "EnergyModel.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
struct PowerMeterData;
namespace hp {
class EnergyService {
public:
    static EnergyService& instance();
    bool addProvider(EnergyProvider* provider); // Setup/worker only, max eight.
    void refresh(); // Existing background worker, cached providers only.
    void publishPower(const PowerMeterData& data);
    bool trySnapshot(EnergySnapshot& result);
private:
    EnergyProvider* providers_[8]{};size_t count_=0;
    EnergySnapshot power_,snapshot_;
    SemaphoreHandle_t mutex_=nullptr;
};
}
