#pragma once
#include <cstdint>
namespace hp {
constexpr uint32_t kLuaDefaultTickMs=750;
constexpr bool luaTickIntervalValid(uint32_t ms){return ms>=100&&ms<=2000;}
constexpr uint32_t luaTickPeriod(uint32_t requested,uint32_t penalty){return requested>penalty?requested:penalty;}
// Allow at most five percent duty per requested tick, bounded below the
// 12-ms VM deadline. Small cooperative UI slices are not runaway polling.
constexpr uint32_t luaTickSoftBudgetUs(uint32_t requested){
    const uint32_t duty=requested*50u;
    return duty<4000u?4000u:duty>8000u?8000u:duty;
}
constexpr bool luaTickShouldBackoff(unsigned streak){return streak>=3;}
constexpr uint32_t luaTickBackoff(uint32_t requested,unsigned streak){
    // Scale to the app's requested cadence instead of turning a 150-ms
    // live view into a two-second slideshow. The hard callback budget remains.
    const uint32_t scaled=requested*(streak>=3?4u:2u);
    const uint32_t minimum=scaled<250?250:(scaled>1000?1000:scaled);
    return requested>minimum?requested:minimum;
}
}
