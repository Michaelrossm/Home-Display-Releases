#pragma once
struct lua_State;
namespace hp { void registerLuaUiBridge(::lua_State* L); }
namespace hp { void registerBasicLuaUiBridge(::lua_State* L); }
namespace hp { void protectLuaUiBridge(::lua_State* L); }
