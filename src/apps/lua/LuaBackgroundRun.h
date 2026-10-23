#pragma once
#include <cstdint>
struct lua_State;
namespace hp {
// SDK-independent execution fence, also exercised with the real host Lua VM.
using LuaBackgroundClock=uint32_t(*)();
constexpr uint32_t kLuaBackgroundUs=10000;
constexpr int kLuaBackgroundInstructions=20000;
void protectLuaBackgroundCalls(lua_State* L);
bool luaBackgroundActive(lua_State* L);
bool runLuaBackground(lua_State* L,LuaBackgroundClock clock,const char* event=nullptr,uint32_t job=0,uint32_t* elapsed=nullptr);
}
