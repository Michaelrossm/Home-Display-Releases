#include "OtaTrace.h"
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
namespace hp {
namespace {
struct Retained { uint32_t magic, phase, bytes; int32_t error; uint32_t check; };
RTC_NOINIT_ATTR Retained retained;
portMUX_TYPE traceMux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t magic=0x48504f54;
uint32_t checksum(const Retained& r) { return r.magic ^ r.phase ^ r.bytes ^ static_cast<uint32_t>(r.error) ^ 0x39c71a02; }
}
void OtaTrace::record(Phase phase,uint32_t bytes,int32_t error) {
    portENTER_CRITICAL(&traceMux);
    retained={magic,phase,bytes,error,0};
    retained.check=checksum(retained);
    portEXIT_CRITICAL(&traceMux);
}
OtaTrace::Snapshot OtaTrace::snapshot() {
    portENTER_CRITICAL(&traceMux);
    const auto copy=retained;
    portEXIT_CRITICAL(&traceMux);
    if(copy.magic!=magic || copy.phase>Failed || copy.check!=checksum(copy)) return {};
    return {copy.phase,copy.bytes,copy.error};
}
}
