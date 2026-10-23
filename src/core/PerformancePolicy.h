#pragma once
#include <cstdint>

namespace hp::performance {
// One central, compile-time reversible policy. No per-app tasks.
#ifndef HOMEDISPLAY_SEPARATE_CORES
#define HOMEDISPLAY_SEPARATE_CORES 1
#endif
constexpr int uiCoreFor(unsigned cores, bool separate=true) {
    return separate && cores>1 ? 1 : 0;
}
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
constexpr unsigned kCpuCount=1;
#else
constexpr unsigned kCpuCount=2;
#endif
constexpr int kUiCore=HOMEDISPLAY_SEPARATE_CORES ? uiCoreFor(kCpuCount) : -1;
// -1 is our neutral sentinel; map it to tskNO_AFFINITY for FreeRTOS/HTTPD.
constexpr int kIoCore=HOMEDISPLAY_SEPARATE_CORES ? 0 : -1;
constexpr bool radioBurst(bool active, unsigned queued, bool seen,
                          uint32_t last, uint32_t now) {
    return active || queued || (seen && uint32_t(now-last)<5000u);
}
}
