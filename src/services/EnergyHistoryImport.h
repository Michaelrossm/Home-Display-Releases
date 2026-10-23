#pragma once
#include "../apps/lua/LuaJobService.h"
namespace hp {
// Owner-scoped native handlers on the existing transfer worker only.
void executeEnergyHistorySync(const LuaJobService::Request&,LuaJobService::Result&);
void executeEnergyHistoryQuery(const LuaJobService::Request&,LuaJobService::Result&);
bool energyTrackerCredentials(const String& owner,unsigned index,String& host,String& password);
}
