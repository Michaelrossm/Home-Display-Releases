/* Host-only test runner, linked against the pinned firmware Lua. */
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    lua_State *L = luaL_newstate();
    if (!L) return 2;
    luaL_openlibs(L);
    lua_pushstring(L, argv[2]);
    lua_setglobal(L, "app_path");
    int rc = luaL_loadfilex(L, argv[1], "t");
    if (rc == LUA_OK) rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return rc == LUA_OK ? 0 : 1;
}
