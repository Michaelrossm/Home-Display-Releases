#pragma once
#include <Arduino.h>
struct lua_State;
namespace hp {
void registerLuaDiscoveryBridge(::lua_State*);
String executeLuaDiscovery(const String& services);
}
