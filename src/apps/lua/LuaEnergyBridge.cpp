#include "LuaEnergyBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "../../services/EnergyService.h"
#include "../../services/EnergyHistoryService.h"
#include "../../services/EnergyHistoryImport.h"
#include "../power/PowerService.h"
#include "../../services/SettingsService.h"
#include "../../services/ConfigService.h"
#include "../../services/SecurityService.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
int detailStatus(lua_State* L){EnergyDetailStatus s;if(!EnergyHistoryService::instance().tryDetailStatus(s)){lua_pushnil(L);return 1;}
    lua_newtable(L);
    auto number=[&](const char* key,uint64_t v){lua_pushnumber(L,static_cast<lua_Number>(v));lua_setfield(L,-2,key);};
    auto flag=[&](const char* key,bool v){lua_pushboolean(L,v);lua_setfield(L,-2,key);};
    flag("enabled",s.enabled);flag("sd_available",s.sdAvailable);flag("usb_available",s.usbAvailable);flag("backfill_supported",s.backfillSupported);flag("low_space",s.lowSpace);
    number("interval_minutes",s.intervalMinutes);number("retention_days",s.retentionDays);number("buffer_points",s.bufferPoints);number("buffered_minutes",s.bufferedMinutes);
    number("last_flush",s.lastFlush);number("last_backfill",s.lastBackfill);number("stored_days",s.storedDays);number("bytes_used",s.bytesUsed);number("free_bytes",s.freeBytes);
    number("dropped_points",s.droppedPoints);number("write_errors",s.writeErrors);number("recovered_blocks",s.recoveredBlocks);number("flush_bytes",s.flushBytes);number("flushes",s.flushes);
    number("aggregationUs",s.aggregationUs);number("aggregationSamples",s.aggregationSamples);number("maxFlushUs",s.maxFlushUs);
    lua_pushstring(L,s.backfillSource);lua_setfield(L,-2,"backfill_source");lua_pushstring(L,!s.enabled?"disabled":!s.sdAvailable?"SD nicht verfuegbar":s.lowSpace?"SD Speicher knapp":"ready");lua_setfield(L,-2,"status");
    const auto estimate=detailEstimate(s.intervalMinutes);number("recordBytes",estimate.recordBytes);number("pointsPerDay",estimate.pointsPerDay);number("bytesPerMonth",estimate.bytesPerMonth);number("bytesPerYear",estimate.bytesPerYear);return 1;
}
void value(lua_State* L,const char* name,const EnergyValue& v) {
    if(v.available())lua_pushnumber(L,v.value);else lua_pushnil(L);lua_setfield(L,-2,name);
}
int queueHistory(lua_State* L,bool external) {
    auto* app=LuaApp::self(L);LuaJobService::Request request;request.kind=external?5:3;request.url=luaL_optstring(L,1,"today");
    if(external){request.required=perm(AppPermission::Energy);request.handler=executeEnergyHistoryQuery;request.lane=LuaJobService::Request::Lane::LocalTransfer;request.completion="energy.history";}
    if(app){request.owner=app->id();request.permissions=app->permissions();}
    const uint32_t id=app?LuaJobService::instance().submit(request):0;if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int history(lua_State* L){return queueHistory(L,false);}
int archiveHistory(lua_State* L){return queueHistory(L,true);}
int syncHistory(lua_State* L){
 auto* app=LuaApp::self(L);const auto index=luaL_checkinteger(L,1);
 LuaJobService::Request request;request.kind=5;request.handler=executeEnergyHistorySync;
 request.required=perm(AppPermission::Energy)|perm(AppPermission::Network)|perm(AppPermission::UserStorage)|perm(AppPermission::Secrets);
 request.resource=index;request.lane=LuaJobService::Request::Lane::Transfer;request.completion="energy.history";
 if(app){request.owner=app->id();request.permissions=app->permissions();}
 const auto job=app&&index>=1&&index<=8?LuaJobService::instance().submit(request):0;
 if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;
}
void executeBind(const LuaJobService::Request& request,LuaJobService::Result& result){
 if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
 result.ok=PowerService::instance().bindSource(request.owner,request.resource);
 if(request.cancelled&&request.cancelled->load()){PowerService::instance().cleanupOwner(request.owner.c_str(),false);result.ok=false;result.error="cancelled";}
 if(!result.ok&&!result.error.length())result.error="energy_source_bind";
}
int bindSource(lua_State* L){auto* app=LuaApp::self(L);const auto index=luaL_checkinteger(L,1);
 LuaJobService::Request request;request.kind=5;request.handler=executeBind;
 request.required=perm(AppPermission::Energy)|perm(AppPermission::Network)|perm(AppPermission::UserStorage)|perm(AppPermission::Secrets);
 request.resource=index;request.lane=LuaJobService::Request::Lane::LocalTransfer;request.completion="energy.history";
 if(app){request.owner=app->id();request.permissions=app->permissions();}
 const auto job=app&&index>=1&&index<=8?LuaJobService::instance().submit(request):0;if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;
}
int archiveSettings(lua_State* L){const auto s=SettingsService::instance().snapshot();lua_newtable(L);
 lua_pushboolean(L,s.energyDetailHistoryEnabled);lua_setfield(L,-2,"enabled");lua_pushinteger(L,s.energyDetailHistoryIntervalMinutes);lua_setfield(L,-2,"interval_minutes");return 1;}
void executeArchiveSettings(const LuaJobService::Request& request,LuaJobService::Result& result){
 if(!SecurityService::instance().settingsAllowed()){result.error="settings_locked";return;}
 auto& config=ConfigService::instance();result.ok=config.putU8("ed_interval",request.resource)&&config.putBool("ed_enabled",request.method!=0);
 if(!result.ok)result.error="archive_settings_write";
}
int configureArchive(lua_State* L){auto* app=LuaApp::self(L);luaL_checktype(L,1,LUA_TBOOLEAN);const bool enabled=lua_toboolean(L,1);const auto interval=luaL_checkinteger(L,2);
 LuaJobService::Request request;request.kind=5;request.handler=executeArchiveSettings;request.required=perm(AppPermission::Energy)|perm(AppPermission::UserStorage);
 request.resource=interval;request.method=enabled;request.lane=LuaJobService::Request::Lane::LocalTransfer;request.completion="energy.history";
 if(app){request.owner=app->id();request.permissions=app->permissions();}
 const auto job=app&&interval>=1&&interval<=30&&SecurityService::instance().settingsAllowed()?LuaJobService::instance().submit(request):0;
 if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;
}
int revision(lua_State* L){EnergySnapshot s;if(!EnergyService::instance().trySnapshot(s)){lua_pushnil(L);return 1;}
    lua_pushnumber(L,s.revision);lua_pushnumber(L,s.updatedMs);lua_pushboolean(L,false);lua_pushboolean(L,s.online);return 4;
}
int snapshot(lua_State* L) {
    EnergySnapshot s;if(!EnergyService::instance().trySnapshot(s)){lua_pushnil(L);return 1;}
    lua_newtable(L);lua_pushboolean(L,s.online);lua_setfield(L,-2,"online");lua_pushboolean(L,s.online);lua_setfield(L,-2,"valid");
    lua_pushstring(L,s.status);lua_setfield(L,-2,"status");lua_pushstring(L,s.source);lua_setfield(L,-2,"source");
    lua_pushinteger(L,s.timestamp);lua_setfield(L,-2,"timestamp");lua_pushinteger(L,s.revision);lua_setfield(L,-2,"revision");
    lua_pushinteger(L,s.updatedMs);lua_setfield(L,-2,"updated_ms");
    lua_pushboolean(L,!strcmp(s.status,"stale"));lua_setfield(L,-2,"stale");
    const char* names[]={"gridPower","housePower","pvPower","batteryPower","batterySoc","gridImportTotal","gridExportTotal","pvEnergyToday","houseEnergyToday","batteryChargeToday","batteryDischargeToday"};
    const EnergyValue* fields[]={&s.gridPower,&s.housePower,&s.pvPower,&s.batteryPower,&s.batterySoc,&s.gridImportTotal,&s.gridExportTotal,&s.pvEnergyToday,&s.houseEnergyToday,&s.batteryChargeToday,&s.batteryDischargeToday};
    for(unsigned i=0;i<11;++i)value(L,names[i],*fields[i]);
    lua_newtable(L);
    for(unsigned i=0;i<11;++i){lua_pushstring(L,fields[i]->quality==EnergyQuality::Measured?"measured":fields[i]->quality==EnergyQuality::Calculated?"calculated":"unavailable");lua_setfield(L,-2,names[i]);}
    lua_setfield(L,-2,"quality");
    lua_newtable(L);
    for(unsigned i=0;i<3;++i){lua_newtable(L);value(L,"voltage",s.phase[i].voltage);value(L,"current",s.phase[i].current);value(L,"power",s.phase[i].power);lua_rawseti(L,-2,i+1);}
    lua_setfield(L,-2,"phase");return 1;
}
}
void registerLuaEnergyBridge(lua_State* L){auto* app=LuaApp::self(L);if(app&&app->allows(AppPermission::Energy)){lua_register(L,"hp_energy_revision",revision);lua_register(L,"hp_energy_snapshot",snapshot);lua_register(L,"hp_energy_history",history);lua_register(L,"hp_energy_sync_history",syncHistory);lua_register(L,"hp_energy_bind_source",bindSource);lua_register(L,"hp_energy_detail_history_status",detailStatus);
 lua_register(L,"hp_energy_archive_settings",archiveSettings);lua_register(L,"hp_energy_configure_archive",configureArchive);
 lua_register(L,"hp_energy_history_archive",archiveHistory);
 LuaJobService::instance().registerOwnerCleanup([](const char* owner){PowerService::instance().cleanupOwner(owner,LuaJobService::instance().ownerSuspending());});}}
}
