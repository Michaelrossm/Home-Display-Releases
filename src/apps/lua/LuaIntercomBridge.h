#pragma once
struct lua_State;
namespace hp {
    void registerLuaIntercomBridge(lua_State *);
    void pollLuaVideoViews();
} // namespace hp
