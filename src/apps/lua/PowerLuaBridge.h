#pragma once
struct lua_State;
namespace hp {
// Domain-specific energy bridge; UI lives in LuaUiBridge.
void registerPowerLuaBridge(::lua_State* L);
}
