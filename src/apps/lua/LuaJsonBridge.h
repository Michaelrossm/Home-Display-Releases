#pragma once
#include <cstddef>
struct lua_State;
class String;
namespace hp {
void registerLuaJsonBridge(::lua_State*);
int pushLuaJson(::lua_State*,const char*,size_t);
// Native bounded schemas may exceed the public JSON API's 512-element limit.
int pushLuaJsonBounded(::lua_State*,const char*,size_t,size_t maxElements);
bool encodeLuaJson(::lua_State*,int,String&);
}
