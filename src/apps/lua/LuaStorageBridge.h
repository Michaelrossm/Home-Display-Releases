#pragma once
#include <Arduino.h>
struct lua_State;
namespace hp {
struct LuaStorageResult { bool ok=false; String value,error; int size=0; };
void registerLuaStorageBridge(::lua_State*);
LuaStorageResult executeLuaStorage(const String& owner,uint8_t operation,const String& name,const String& value);
String luaSecretForNative(const String& owner,const String& name);
String luaStringForNative(const String& owner,const String& name);
}
