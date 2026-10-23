#pragma once
#include "../apps/lua/LuaJobService.h"
namespace hp {
// Shared bounded worker adapter for native and Lua consumers.
void executePostalLookupJob(const LuaJobService::Request&,LuaJobService::Result&);
}
