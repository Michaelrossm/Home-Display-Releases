#pragma once
struct lua_State;
namespace hp {
void registerLuaMediaStorageBridge(lua_State* L);
void installLuaMediaStorageTables(lua_State* L); // After common UI/background fences.
void pollLuaWallpaperPreview(); // Existing UI pass, under DisplayUiGuard; no timer/task.
}
