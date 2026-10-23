#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <cstring>
struct StaticQueue_t{size_t size=0,capacity=0;std::deque<std::vector<uint8_t>> items;};
using QueueHandle_t=StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t count,size_t size,uint8_t*,StaticQueue_t* q){q->size=size;q->capacity=count;return q;}
inline int xQueueSend(QueueHandle_t q,const void* item,unsigned){if(q->items.size()>=q->capacity)return 0;const auto* p=static_cast<const uint8_t*>(item);q->items.emplace_back(p,p+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* item,unsigned){if(q->items.empty())return 0;memcpy(item,q->items.front().data(),q->size);q->items.pop_front();return 1;}
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q){return q->items.size();}
