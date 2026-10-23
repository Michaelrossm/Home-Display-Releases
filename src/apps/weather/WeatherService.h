#pragma once
#include "../../core/BackgroundScheduler.h"
#include "engine/WeatherClient.h"
#include <freertos/semphr.h>
namespace hp { class WeatherService:public IBackgroundJob { public: static WeatherService& instance(); bool begin(); void backgroundTick()override; WeatherData snapshot(); void force(){last_=0;} private: static void updateJob(void*); void executeUpdate(); WeatherClient client_; WeatherData data_; SemaphoreHandle_t mux_=nullptr; uint32_t last_=0; bool updateQueued_=false; }; }
