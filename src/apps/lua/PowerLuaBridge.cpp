#include "PowerLuaBridge.h"
#include "LuaBridgeContract.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <lvgl.h>
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include "../power/PowerService.h"
#include "../../services/SettingsService.h"
#include "../../services/TimeService.h"
#include "../../ui/UiTheme.h"
namespace hp { namespace {
void pushNumberOrNil(lua_State* L,float value){if(std::isfinite(value))lua_pushnumber(L,value);else lua_pushnil(L);}
void setNumberField(lua_State* L,const char* key,float value){pushNumberOrNil(L,value);lua_setfield(L,-2,key);}
int bridgePowerSnapshot(lua_State* L){
 PowerMeterData d;const bool valid=PowerService::instance().trySnapshot(d);lua_newtable(L);
 lua_pushboolean(L,valid);lua_setfield(L,-2,"valid");if(!valid)return 1;
 lua_pushboolean(L,d.online);lua_setfield(L,-2,"online");
 setNumberField(L,"total_power",d.totalPower);setNumberField(L,"import_kwh",d.totalImportKwh);setNumberField(L,"export_kwh",d.totalExportKwh);
 const uint32_t age=d.updatedMs&&millis()>=d.updatedMs?millis()-d.updatedMs:0;
 lua_pushinteger(L,age);lua_setfield(L,-2,"age_ms");lua_pushstring(L,d.error.c_str());lua_setfield(L,-2,"error");
 for(int i=0;i<3;++i){lua_newtable(L);setNumberField(L,"voltage",d.voltage[i]);setNumberField(L,"current",d.current[i]);setNumberField(L,"power",d.power[i]);const String key="l"+String(i+1);lua_setfield(L,-2,key.c_str());}return 1;
}
void pushArray(lua_State* L,const float* values,int count){lua_createtable(L,count,0);for(int i=0;i<count;++i){lua_pushnumber(L,values[i]);lua_rawseti(L,-2,i+1);}}
int bridgePowerSeries(lua_State* L){
 const char* range=luaL_optstring(L,1,"today");PowerMeterHistoryView h;
 const bool valid=PowerService::instance().tryHistory(h);lua_newtable(L);lua_pushboolean(L,valid);lua_setfield(L,-2,"valid");if(!valid)return 1;
 float imports[24]{},exports[24]{};int points=24;String caption="Heute · 0-23 Uhr · Werte in Wh";bool any=false;
 time_t now=TimeService::instance().nowUtc();tm local{};const bool timeOk=TimeService::instance().localFromEpoch(now,local);
 if(!strcmp(range,"today")){
  if(timeOk){const int hour=local.tm_hour;imports[hour]=std::max(0.0f,h.currentHourImportWh);exports[hour]=std::max(0.0f,h.currentHourExportWh);
   const int completed=std::min<int>(hour,h.hourlyCount);for(int back=1;back<=completed;++back){const int src=h.hourlyCount-back,dst=hour-back;imports[dst]=std::max(0.0f,h.hourlyImportWh[src]);exports[dst]=std::max(0.0f,h.hourlyExportWh[src]);}}
  else{const int count=std::min<int>(24,h.hourlyCount);for(int i=0;i<count;++i){int src=h.hourlyCount-count+i;imports[i]=std::max(0.0f,h.hourlyImportWh[src]);exports[i]=std::max(0.0f,h.hourlyExportWh[src]);}caption="Letzte Stunden · Werte in Wh";}
 }else if(!strcmp(range,"week")){
  points=7;caption="Mo-So · Tageswerte in Wh";
  if(timeOk){const int monday=(local.tm_wday+6)%7;imports[monday]=std::max(0.0f,h.currentDayImportWh);exports[monday]=std::max(0.0f,h.currentDayExportWh);const int completed=std::min<int>(monday,h.dailyCount);
   for(int back=1;back<=completed;++back){const int src=h.dailyCount-back,dst=monday-back;imports[dst]=std::max(0.0f,h.dailyImportWh[src]);exports[dst]=std::max(0.0f,h.dailyExportWh[src]);}}
  else{const int count=std::min<int>(7,h.dailyCount);for(int i=0;i<count;++i){const int src=h.dailyCount-count+i;imports[i]=std::max(0.0f,h.dailyImportWh[src]);exports[i]=std::max(0.0f,h.dailyExportWh[src]);}}
 }else{
  points=12;caption="Jan-Dez · Monatswerte in Wh";
  if(timeOk){imports[local.tm_mon]+=std::max(0.0f,h.currentDayImportWh);exports[local.tm_mon]+=std::max(0.0f,h.currentDayExportWh);
   tm cursor=local;cursor.tm_hour=12;cursor.tm_min=0;cursor.tm_sec=0;cursor.tm_isdst=-1;
   for(int back=1;back<=h.dailyCount;++back){cursor.tm_mday-=1;const time_t t=mktime(&cursor);if(t<=0)break;tm normalized{};if(!TimeService::instance().localFromEpoch(t,normalized))break;cursor=normalized;if(normalized.tm_year!=local.tm_year)break;const int src=h.dailyCount-back;imports[normalized.tm_mon]+=std::max(0.0f,h.dailyImportWh[src]);exports[normalized.tm_mon]+=std::max(0.0f,h.dailyExportWh[src]);}}
  else caption="Jahr · Zeit noch nicht synchronisiert";
 }
 for(int i=0;i<points;++i)any=any||imports[i]>0.0f||exports[i]>0.0f;
 pushArray(L,imports,points);lua_setfield(L,-2,"import");pushArray(L,exports,points);lua_setfield(L,-2,"export");
 lua_pushboolean(L,any);lua_setfield(L,-2,"has_data");lua_pushstring(L,caption.c_str());lua_setfield(L,-2,"caption");
 setNumberField(L,"today_import_kwh",h.currentDayImportWh/1000.0f);setNumberField(L,"today_export_kwh",h.currentDayExportWh/1000.0f);
 setNumberField(L,"avg_import_kwh_day",h.averageImportKwhPerDay);setNumberField(L,"avg_export_kwh_day",h.averageExportKwhPerDay);return 1;
}
int bridgePowerSettingsGet(lua_State* L){const auto s=SettingsService::instance().snapshot();lua_newtable(L);lua_pushstring(L,s.powerHost.c_str());lua_setfield(L,-2,"host");lua_pushboolean(L,s.powerPassword.length()>0);lua_setfield(L,-2,"has_password");return 1;}
int bridgePowerSettingsSave(lua_State* L){String host=luaL_optstring(L,1,"");const String password=luaL_optstring(L,2,"");host.trim();if(host.startsWith("https://")){lua_pushboolean(L,false);return 1;}if(host.startsWith("http://"))host.remove(0,7);while(host.endsWith("/"))host.remove(host.length()-1);
 if(!host.length()||host.length()>96||host.indexOf('/')>=0||host.indexOf('?')>=0||host.indexOf('#')>=0||host.indexOf('@')>=0){lua_pushboolean(L,false);return 1;}
 auto s=SettingsService::instance().snapshot();s.powerHost=host;if(password.length())s.powerPassword=password;SettingsService::instance().save(s);lua_pushboolean(L,true);return 1;}
int bridgePowerDiscoveryStart(lua_State* L){lua_pushboolean(L,PowerService::instance().requestDiscovery());return 1;}
int bridgePowerDiscoveryStatus(lua_State* L){String status;if(!PowerService::instance().tryDiscoveryStatus(status))status="";lua_pushstring(L,status.c_str());return 1;}
int bridgePowerDiscoveryDevices(lua_State* L){PowerDiscoveryDevice devices[4]{};const int count=PowerService::instance().tryCopyDiscovered(devices,4);lua_createtable(L,count,0);for(int i=0;i<count;++i){lua_newtable(L);lua_pushstring(L,devices[i].host);lua_setfield(L,-2,"host");lua_pushstring(L,devices[i].name);lua_setfield(L,-2,"name");lua_rawseti(L,-2,i+1);}return 1;}
}}
void hp::registerPowerLuaBridge(lua_State* L){
 lua_register(L,"hp_power_snapshot",bridgePowerSnapshot);lua_register(L,"hp_power_series",bridgePowerSeries);
 lua_register(L,"hp_power_settings_get",bridgePowerSettingsGet);lua_register(L,"hp_power_settings_save",bridgePowerSettingsSave);
 lua_register(L,"hp_power_discovery_start",bridgePowerDiscoveryStart);lua_register(L,"hp_power_discovery_status",bridgePowerDiscoveryStatus);lua_register(L,"hp_power_discovery_devices",bridgePowerDiscoveryDevices);
 luaBridgeContract::registerCapabilities(L);
 for(const char* f:{"hp_power_snapshot","hp_power_series","hp_power_settings_get","hp_power_discovery_status","hp_power_discovery_devices"})luaBridgeContract::wrap(L,f,luaBridgeContract::Kind::Query);
 for(const char* f:{"hp_power_settings_save","hp_power_discovery_start"})luaBridgeContract::wrap(L,f,luaBridgeContract::Kind::Action);
}
