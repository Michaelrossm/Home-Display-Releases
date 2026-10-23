#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
using UBaseType_t=unsigned;using TaskHandle_t=void*;using portMUX_TYPE=int;
constexpr int portMUX_INITIALIZER_UNLOCKED=0,pdTRUE=1,pdPASS=1,tskNO_AFFINITY=-1,portMAX_DELAY=-1;
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdMS_TO_TICKS(x) (x)
struct Queue{size_t capacity,bytes;std::deque<std::vector<uint8_t>> jobs;};using QueueHandle_t=Queue*;
QueueHandle_t xQueueCreate(size_t n,size_t bytes){return new Queue{n,bytes,{}};}
void vQueueDelete(QueueHandle_t q){delete q;}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){return unsigned(q->jobs.size());}
int xQueueSend(QueueHandle_t q,const void* job,int wait){assert(!wait);if(q->jobs.size()==q->capacity)return 0;
 q->jobs.emplace_back(static_cast<const uint8_t*>(job),static_cast<const uint8_t*>(job)+q->bytes);return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void* job,int wait){assert(!wait);if(q->jobs.empty())return 0;
 memcpy(job,q->jobs.front().data(),q->bytes);q->jobs.pop_front();return pdTRUE;}
uintptr_t nextTask=1;TaskHandle_t currentTask=reinterpret_cast<void*>(1);
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t* target,int){*target=reinterpret_cast<void*>(nextTask++);return pdPASS;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
int xPortGetCoreID(){return 0;}unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 1;}
unsigned notifications=0;void xTaskNotifyGive(TaskHandle_t){++notifications;}
struct Drained{};
unsigned ulTaskNotifyTake(int clear,unsigned wait){assert(clear&&wait==50);throw Drained{};}
uint32_t now=0;uint32_t millis(){return now;}
size_t strlcpy(char* out,const char* in,size_t n){const auto size=strlen(in);if(n){memcpy(out,in,std::min(size,n-1));out[std::min(size,n-1)]=0;}return size;}
struct SerialFake{void printf(const char*,...) {}} Serial;
namespace hp {
namespace performance{constexpr int kIoCore=0;bool radioBurst(bool active,unsigned queued,bool seen,uint32_t last,uint32_t now){return active||queued||(seen&&uint32_t(now-last)<5000u);}}
enum class ServiceId{Network};
struct SystemSupervisor{static SystemSupervisor& instance(){static SystemSupervisor s;return s;}void heartbeat(ServiceId){}};
struct EventLog{static EventLog& instance(){static EventLog e;return e;}void add(const char*,const char*,const char*){}};
}
