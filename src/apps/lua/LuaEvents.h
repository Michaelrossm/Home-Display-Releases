#pragma once
#include <stdint.h>
namespace hp {
// Fixed-size MPSC queue. Publication never waits and never invokes Lua.
struct LuaEvent { char owner[32]; char name[48]; uint32_t job; uint32_t created=0,retryAfter=0; };
bool publishLuaEvent(const char* owner,const char* name,uint32_t job=0);
bool receiveLuaEvent(LuaEvent& event);
bool deferLuaEvent(LuaEvent event); // Preserve age; one-second retry, bounded by result TTL.
void discardLuaEvents(const char* owner);
uint32_t droppedLuaEvents();
}
