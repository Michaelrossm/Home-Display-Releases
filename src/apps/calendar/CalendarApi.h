#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include "../lua/LuaJobService.h"
namespace hp {
// Worker-only API shared by Lua and the authenticated web settings page.
bool executeCalendarOperation(const char* operation,JsonVariantConst arguments,JsonDocument& result,String& error,const std::atomic<bool>* cancelled=nullptr);
void executeCalendarJob(const LuaJobService::Request& request, LuaJobService::Result& result);
void completeCalendarJob(JsonDocument& output, LuaJobService::Result& result);
}
