#include "LuaEvents.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
namespace hp {
namespace {
StaticQueue_t control;
std::atomic<uint32_t> dropped{0};
alignas(LuaEvent) uint8_t storage[16*sizeof(LuaEvent)];
QueueHandle_t queue() { static QueueHandle_t q=xQueueCreateStatic(16,sizeof(LuaEvent),storage,&control);return q; }
}
bool publishLuaEvent(const char* owner,const char* name,uint32_t job) {
    if(!name||strlen(name)>=sizeof(LuaEvent::name)|| (owner&&strlen(owner)>=sizeof(LuaEvent::owner)))return false;
    LuaEvent event{};strlcpy(event.owner,owner?owner:"",sizeof(event.owner));strlcpy(event.name,name,sizeof(event.name));event.job=job;event.created=millis();
    const bool sent=xQueueSend(queue(),&event,0)==pdTRUE;if(!sent)dropped.fetch_add(1,std::memory_order_relaxed);return sent;
}
bool receiveLuaEvent(LuaEvent& event){return xQueueReceive(queue(),&event,0)==pdTRUE;}
bool deferLuaEvent(LuaEvent event){const uint32_t now=millis();if(uint32_t(now-event.created)>=60000){++dropped;return false;}
    if(!event.retryAfter||static_cast<int32_t>(now-event.retryAfter)>=0)event.retryAfter=now+1000;const bool sent=xQueueSend(queue(),&event,0)==pdTRUE;if(!sent)++dropped;return sent;}
uint32_t droppedLuaEvents(){return dropped.load(std::memory_order_relaxed);}
void discardLuaEvents(const char* owner){if(!owner)return;const UBaseType_t count=uxQueueMessagesWaiting(queue());LuaEvent e{};
    for(UBaseType_t i=0;i<count&&receiveLuaEvent(e);++i)if(strcmp(e.owner,owner)&&xQueueSend(queue(),&e,0)!=pdTRUE)++dropped;
}
}
