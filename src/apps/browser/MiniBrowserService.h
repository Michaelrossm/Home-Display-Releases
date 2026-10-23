#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace hp { struct MiniPage{bool ok=false;int status=0;String url,title,text,error;}; enum class MiniBrowserState:uint8_t{Idle,Queued,Loading,Ready,Error}; class MiniBrowserService{public:static MiniBrowserService&instance();bool begin();bool request(const char*url);MiniBrowserState state()const;bool takeResult(MiniPage&out);private:static void jobThunk(void*);void execute();MiniPage fetchSync(const char*url);String htmlToText(const String&,String*);static String decodeEntities(String);mutable SemaphoreHandle_t lock_=nullptr;String pendingUrl_;MiniPage result_;MiniBrowserState state_=MiniBrowserState::Idle;}; }
