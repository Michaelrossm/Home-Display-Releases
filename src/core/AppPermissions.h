#pragma once
#include <stdint.h>
namespace hp {
enum class AppPermission : uint32_t {
    Network=1<<0, Audio=1<<1, UserStorage=1<<2, KeepScreenOn=1<<3,
    Notifications=1<<4, NetworkInternet=1<<5, Secrets=1<<6,
    Discovery=1<<7, Mqtt=1<<8, Websocket=1<<9,
    HomeAssistant=1<<10, Energy=1<<11, StorageRead=1<<12,
    NasRead=1<<13, Wallpaper=1<<14, IdleIntegration=1<<15,
    CalendarRead=1u<<16, CalendarWrite=1u<<17,
    Camera=1u<<18, Microphone=1u<<19, Intercom=1u<<20, DoorOpen=1u<<21
};
using AppPermissions=uint32_t;
constexpr AppPermissions perm(AppPermission p){return static_cast<AppPermissions>(p);}
}
