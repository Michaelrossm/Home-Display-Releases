#include "lua.h"
#include "lauxlib.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* Use the pinned firmware parser; never execute package code. */
int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    int failed = 0;
    lua_State *state = luaL_newstate();
    if (!state) return 2;
    for (int i = 1; i < argc; ++i) {
        const char *path = strcmp(argv[i], "-") == 0 ? NULL : argv[i];
        if (luaL_loadfilex(state, path, "t") != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(state, -1));
            failed = 1;
        }
        lua_settop(state, 0);
    }
    lua_close(state);
    return failed;
}
