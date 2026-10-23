#pragma once
#include <cstdint>
namespace hp {
// Caps only: an empty LVGL dirty list never becomes a frame. No app polling.
constexpr uint32_t uiRefreshPeriod(bool off,bool dimmed,bool saver,bool interaction,bool animation){
    return off?0:interaction?33:dimmed?200:saver?100:animation?33:100;
}
}
