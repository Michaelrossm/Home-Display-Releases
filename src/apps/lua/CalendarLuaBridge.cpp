#include "CalendarLuaBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "LuaJsonBridge.h"
#include "../calendar/CalendarApi.h"
#include "../calendar/CalendarDate.h"
#include "../calendar/CalendarService.h"
#include "../../services/CalendarSyncService.h"
#include "../../services/NetworkService.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {namespace {
int request(lua_State* L){
 auto* app=LuaApp::self(L);const char* op=lua_tostring(L,lua_upvalueindex(1));const bool write=lua_toboolean(L,lua_upvalueindex(2));
 if(!app||!app->allows(write?AppPermission::CalendarWrite:AppPermission::CalendarRead)||(write&&app->lifecycle()==LuaRuntimeState::BackgroundExecuting)){lua_pushnil(L);return 1;}
 LuaJobService::Request job;job.owner=app->id();job.permissions=app->permissions();job.required=perm(write?AppPermission::CalendarWrite:AppPermission::CalendarRead);job.kind=5;job.handler=executeCalendarJob;job.context=op;job.completion="calendar.completed";
 if(lua_isnoneornil(L,1))job.body="{}";else if(!lua_istable(L,1)||!encodeLuaJson(L,1,job.body)){lua_pushnil(L);return 1;}
 if(!strcmp(op,"query"))job.fileName="calendar_query";
 if(!strcmp(op,"people")||!strcmp(op,"user_profile")||!strcmp(op,"person_save")||!strcmp(op,"person_remove"))job.lane=LuaJobService::Request::Lane::LocalTransfer;
 if(job.body.length()>LuaJobService::kBodyBytes){lua_pushnil(L);return 1;}
 const auto id=LuaJobService::instance().submit(job);if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int date(lua_State* L){
 luaL_checktype(L,1,LUA_TTABLE);auto field=[&](const char* key,int fallback){lua_getfield(L,1,key);const int v=lua_isnil(L,-1)?fallback:lua_tointeger(L,-1);lua_pop(L,1);return v;};
 const calendar::Date d{field("year",0),field("month",0),field("day",0)};const auto epoch=calendar::localEpoch(d,field("hour",0),field("minute",0));
 if(epoch<=0||epoch>INT32_MAX){lua_pushnil(L);return 1;}lua_newtable(L);lua_pushinteger(L,epoch);lua_setfield(L,-2,"epoch");lua_pushinteger(L,calendar::weekday(d.year,d.month,d.day));lua_setfield(L,-2,"weekday");lua_pushinteger(L,calendar::daysInMonth(d.year,d.month));lua_setfield(L,-2,"days_in_month");return 1;
}
int capabilities(lua_State* L){
 auto* app=LuaApp::self(L);lua_newtable(L);
 auto flag=[&](const char* key,bool value){lua_pushboolean(L,value);lua_setfield(L,-2,key);};
 lua_pushinteger(L,1);lua_setfield(L,-2,"version");flag("read",app&&app->allows(AppPermission::CalendarRead));flag("local_write",app&&app->allows(AppPermission::CalendarWrite));
 flag("cloud_write",false);flag("caldav_events",true);flag("reminders",true);flag("contact_birthdays",false);
 flag("cursor_pagination",true);
 lua_pushinteger(L,32);lua_setfield(L,-2,"page_limit");lua_pushinteger(L,256);lua_setfield(L,-2,"offset_limit");lua_pushinteger(L,INT32_MAX);lua_setfield(L,-2,"ui_max_epoch");return 1;
}
int revision(lua_State* L){auto* app=LuaApp::self(L);if(!app||!app->allows(AppPermission::CalendarRead)){lua_pushnil(L);return 1;}
 char token[32];auto& sync=CalendarSyncService::instance();snprintf(token,sizeof(token),"%lu:%lu",(unsigned long)CalendarService::instance().syncRevision(),(unsigned long)sync.dataRevision());lua_pushstring(L,token);lua_pushnil(L);
 lua_pushboolean(L,sync.state()==CalendarSyncState::Syncing||sync.state()==CalendarSyncState::Pending);lua_pushboolean(L,NetworkService::instance().online());return 4;
}
}
void registerCalendarLuaBridge(lua_State* L){
 struct Operation{const char* name;const char* op;bool write;};
 for(const auto& operation:{Operation{"hp_calendar_people","people",false},{"hp_calendar_calendars","calendars",false},{"hp_calendar_query","query",false},{"hp_calendar_details","details",false},{"hp_calendar_save","save",true},{"hp_calendar_delete","delete",true},{"hp_calendar_visibility","visibility",true},{"hp_calendar_appearance","appearance",true},{"hp_calendar_region","region",true},{"hp_calendar_person_save","person_save",true},{"hp_users_list","people",false},{"hp_user_profile","user_profile",false},{"hp_user_save","person_save",true},{"hp_user_remove","person_remove",true},{"hp_calendar_create","calendar_create",true},{"hp_calendar_sync","sync",false},{"hp_calendar_status","status",false}}){
  lua_pushstring(L,operation.op);lua_pushboolean(L,operation.write);lua_pushcclosure(L,request,2);lua_setglobal(L,operation.name);
 }
 lua_register(L,"hp_calendar_date",date);
 lua_register(L,"hp_calendar_revision",revision);
 lua_register(L,"hp_users_revision",revision);
 lua_register(L,"hp_calendar_capabilities",capabilities);
}
}
