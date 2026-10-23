#include "RadioLuaBridge.h"
#include "LuaBridgeContract.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include <ArduinoJson.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include "../radio/RadioService.h"
#include "../radio/RadioSettings.h"
#include "../../core/NumericUtils.h"
#include "../../services/ImageCodec.h"
#include "../../services/ImageResourceService.h"
#include <cstring>
namespace hp { namespace {
RadioService& radio(){return RadioService::instance();}
void field(lua_State* L,const char* name,const String& v){lua_pushlstring(L,v.c_str(),v.length());lua_setfield(L,-2,name);}
void field(lua_State* L,const char* name,int v){lua_pushinteger(L,v);lua_setfield(L,-2,name);}
void field(lua_State* L,const char* name,bool v){lua_pushboolean(L,v);lua_setfield(L,-2,name);}
int result(lua_State* L,bool ok){lua_pushboolean(L,ok);return 1;}
int snapshot(lua_State* L){bool valid=false;const auto s=radio().snapshot(0,&valid);lua_newtable(L);
 field(L,"valid",valid);field(L,"online",s.online);field(L,"power",s.power);field(L,"mute",s.mute);
 field(L,"playing",s.online&&s.power&&s.playStatus==2);field(L,"play_status",s.playStatus);
 field(L,"volume",s.volume);field(L,"volume_steps",s.volumeSteps);field(L,"volume_percent",numeric::percentOf(s.volume,s.volumeSteps));field(L,"mode",s.mode);
 field(L,"friendly_name",s.friendlyName);field(L,"station",s.station);field(L,"text",s.text);field(L,"artist",s.artist);field(L,"album",s.album);field(L,"artwork_uri",s.graphicUri);field(L,"error",s.error);return 1;}
void discardArtwork(const char* owner,const String& body){
 JsonDocument doc;if(!deserializeJson(doc,body.c_str())){const uint32_t handle=doc["handle"]|0u;if(handle)ImageResourceService::instance().release(owner,handle);}
}
void executeArtwork(const LuaJobService::Request& request,LuaJobService::Result& result){
 // Only a URL copied from the configured radio's cached metadata is accepted.
 // Lua cannot supply a URL, credentials, headers or a different radio profile.
 LuaJobService::Result response;LuaJobService::executeHttp(request,response);
 if(!response.ok){result.error=response.error;return;}
 auto image=ImageCodec::decode(reinterpret_cast<uint8_t*>(const_cast<char*>(response.body.c_str())),response.body.length(),192,192,false,request.cancelled,false,true);
 response.body="";
 if(!image){result.error="radio_artwork_decode_or_limit";return;}
 if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
 const uint32_t handle=ImageResourceService::instance().store(request.owner,image);
 if(!handle){result.error="image_budget";return;}
 JsonDocument doc;doc["handle"]=handle;doc["width"]=image->width;doc["height"]=image->height;
 serializeJson(doc,result.body);result.size=result.body.length();result.ok=true;
 // Cancellation/expiry/release also drops unclaimed handles via discardArtwork.
}
int artwork(lua_State* L){
 auto* app=LuaApp::self(L);bool valid=false;const auto s=radio().snapshot(0,&valid);
 if(!app||!valid||!s.online||!s.power||!s.graphicUri.length()||s.graphicUri.length()>1024){lua_pushnil(L);return 1;}
 for(size_t i=0;i<s.graphicUri.length();++i)if(static_cast<unsigned char>(s.graphicUri[i])<33){lua_pushnil(L);return 1;}
 LuaJobService::Request request;request.owner=app->id();request.url=s.graphicUri;request.kind=5;
 request.required=perm(AppPermission::Audio);request.permissions=app->permissions();
 // Trusted radio-service read, not a grant of general network access to Lua.
 request.permissions|=perm(AppPermission::Network)|perm(AppPermission::NetworkInternet);
 request.timeout=2500;request.responseLimit=65536;request.lane=LuaJobService::Request::Lane::Transfer;
 request.handler=executeArtwork;request.discardResult=discardArtwork;request.completion="image.ready";
 const auto job=LuaJobService::instance().submit(request);if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;
}
int command(lua_State* L){const char* name=luaL_checkstring(L,1);struct Entry{const char* name;MediaCommand command;};
 static const Entry entries[]={{"play_pause",MediaCommand::PlayPause},{"stop",MediaCommand::Stop},{"next",MediaCommand::Next},{"previous",MediaCommand::Previous},{"power_toggle",MediaCommand::PowerToggle},{"mute_toggle",MediaCommand::MuteToggle}};
 for(const auto& e:entries)if(!strcmp(name,e.name))return result(L,radio().command(e.command));return result(L,false);}
int volume(lua_State* L){const lua_Integer v=luaL_checkinteger(L,1);return result(L,radio().setVolume(v<0?0:v>100?100:(int)v));}
int requestModes(lua_State* L){return result(L,radio().requestModes());}
int selectMode(lua_State* L){return result(L,radio().requestSelectMode((int)luaL_checkinteger(L,1)));}
int requestPresets(lua_State* L){return result(L,radio().requestPresets());}
int selectPreset(lua_State* L){return result(L,radio().requestSelectPreset((int)luaL_checkinteger(L,1)));}
int browseRoot(lua_State* L){return result(L,radio().requestBrowseRoot());}
int browseBack(lua_State* L){return result(L,radio().requestBrowseBack());}
int browseSelect(lua_State* L){const lua_Integer index=luaL_checkinteger(L,1);return result(L,index>=0&&index<48&&radio().requestBrowseSelect((size_t)index));}
int busy(lua_State* L){return result(L,radio().catalogBusy(0));}
int revision(lua_State* L){lua_pushinteger(L,radio().catalogRevision(0));return 1;}
int discover(lua_State* L){return result(L,radio().requestAutoDiscover());}
int discoveryStatus(lua_State* L){const String s=radio().discoveryStatus(0);lua_pushstring(L,s.c_str());return 1;}
int modes(lua_State* L){RadioMode items[16];const int n=radio().copyModes(items,16,0);lua_createtable(L,n,0);for(int i=0;i<n;++i){lua_newtable(L);field(L,"key",items[i].key);field(L,"id",items[i].id);field(L,"label",items[i].label);field(L,"selectable",items[i].selectable);lua_rawseti(L,-2,i+1);}return 1;}
int presets(lua_State* L){RadioPreset items[32];const int n=radio().copyPresets(items,32,0);lua_createtable(L,n,0);for(int i=0;i<n;++i){lua_newtable(L);field(L,"key",items[i].key);field(L,"name",items[i].name);lua_rawseti(L,-2,i+1);}return 1;}
int browseItems(lua_State* L){RadioNavItem items[48];const int n=radio().copyBrowse(items,48,0);lua_createtable(L,n,0);for(int i=0;i<n;++i){lua_newtable(L);field(L,"index",i);field(L,"key",items[i].key);field(L,"type",items[i].type);field(L,"subtype",items[i].subtype);field(L,"name",items[i].name);lua_rawseti(L,-2,i+1);}return 1;}
int discovered(lua_State* L){RadioDiscoveryDevice items[8];const int n=radio().copyDiscovered(items,8,0);lua_createtable(L,n,0);for(int i=0;i<n;++i){lua_newtable(L);field(L,"index",i);field(L,"host",String(items[i].host));field(L,"name",String(items[i].name));field(L,"verified",items[i].verified);lua_rawseti(L,-2,i+1);}return 1;}
int settings(lua_State* L){const auto s=RadioSettings::instance().load();lua_newtable(L);field(L,"host",s.host);field(L,"pin_present",s.pin.length()>0);field(L,"parental_enabled",s.parentalEnabled);field(L,"volume_limit_enabled",s.volumeLimitEnabled);field(L,"max_volume_percent",(int)s.maxVolumePercent);field(L,"lock_sources",s.lockSources);field(L,"lock_power_off",s.lockPowerOff);field(L,"lock_display_controls",s.lockDisplayControls);return 1;}
void executeSave(const LuaJobService::Request& request,LuaJobService::Result& result){RadioAppSettings s;
 if(request.method==1){s=RadioSettings::instance().load();s.host=request.url;}
 else{JsonDocument doc;if(deserializeJson(doc,request.body)){result.error="radio_settings_invalid";return;}s.host=doc["host"].as<String>();s.pin=doc["pin"].as<String>();if(!s.pin.length())s.pin=RadioSettings::instance().load().pin;s.parentalEnabled=doc["parental"].as<bool>();s.volumeLimitEnabled=doc["limit"].as<bool>();s.maxVolumePercent=doc["max"].as<int>();s.lockSources=doc["sources"].as<bool>();s.lockPowerOff=doc["power"].as<bool>();s.lockDisplayControls=doc["display"].as<bool>();}
 result.ok=RadioSettings::instance().save(s)&&radio().requestReconfigure();if(!result.ok)result.error="radio_settings_save";}
uint32_t queueSave(lua_State* L,LuaJobService::Request& request){auto* app=LuaApp::self(L);if(!app)return 0;request.owner=app->id();request.permissions=app->permissions();request.required=perm(AppPermission::Audio);request.kind=5;request.handler=executeSave;return LuaJobService::instance().submit(request);}
int save(lua_State* L){RadioAppSettings s;s.host=luaL_checkstring(L,1);s.pin=luaL_checkstring(L,2);s.parentalEnabled=lua_toboolean(L,3);s.volumeLimitEnabled=lua_toboolean(L,4);s.maxVolumePercent=constrain((int)luaL_checkinteger(L,5),5,100);s.lockSources=lua_toboolean(L,6);s.lockPowerOff=lua_toboolean(L,7);s.lockDisplayControls=lua_toboolean(L,8);
 if(s.host.length()>96||s.pin.length()>16){lua_pushboolean(L,false);lua_pushliteral(L,"Host/PIN zu lang");return 2;}
 JsonDocument doc;doc["host"]=s.host;doc["pin"]=s.pin;doc["parental"]=s.parentalEnabled;doc["limit"]=s.volumeLimitEnabled;doc["max"]=s.maxVolumePercent;doc["sources"]=s.lockSources;doc["power"]=s.lockPowerOff;doc["display"]=s.lockDisplayControls;LuaJobService::Request request;serializeJson(doc,request.body);
 const uint32_t job=queueSave(L,request);if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;}
int useDiscovered(lua_State* L){const lua_Integer index=luaL_checkinteger(L,1);RadioDiscoveryDevice items[8];const int n=radio().copyDiscovered(items,8,0);if(index<0||index>=n){lua_pushnil(L);return 1;}LuaJobService::Request request;request.method=1;request.url=items[index].host;const uint32_t job=queueSave(L,request);if(job)lua_pushinteger(L,job);else lua_pushnil(L);return 1;}
}}
void hp::registerRadioLuaBridge(lua_State* L){
 lua_register(L,"hp_radio_snapshot",snapshot);lua_register(L,"hp_radio_command",command);lua_register(L,"hp_radio_set_volume",volume);
 lua_register(L,"hp_radio_artwork",artwork);
 lua_register(L,"hp_radio_request_modes",requestModes);lua_register(L,"hp_radio_modes",modes);lua_register(L,"hp_radio_select_mode",selectMode);
 lua_register(L,"hp_radio_request_presets",requestPresets);lua_register(L,"hp_radio_presets",presets);lua_register(L,"hp_radio_select_preset",selectPreset);
 lua_register(L,"hp_radio_browse_root",browseRoot);lua_register(L,"hp_radio_browse_items",browseItems);lua_register(L,"hp_radio_browse_select",browseSelect);lua_register(L,"hp_radio_browse_back",browseBack);
 lua_register(L,"hp_radio_catalog_busy",busy);lua_register(L,"hp_radio_catalog_revision",revision);lua_register(L,"hp_radio_discover",discover);lua_register(L,"hp_radio_discovery_status",discoveryStatus);lua_register(L,"hp_radio_discovered",discovered);
 lua_register(L,"hp_radio_use_discovered",useDiscovered);lua_register(L,"hp_radio_settings",settings);lua_register(L,"hp_radio_save_settings",save);
 luaBridgeContract::registerCapabilities(L);
 luaBridgeContract::wrap(L,"hp_radio_artwork",luaBridgeContract::Kind::Action);
 for(const char* f:{"hp_radio_snapshot","hp_radio_modes","hp_radio_presets","hp_radio_browse_root","hp_radio_browse_items","hp_radio_catalog_busy","hp_radio_catalog_revision","hp_radio_discovery_status","hp_radio_discovered","hp_radio_settings"})luaBridgeContract::wrap(L,f,luaBridgeContract::Kind::Query);
 for(const char* f:{"hp_radio_command","hp_radio_set_volume","hp_radio_request_modes","hp_radio_select_mode","hp_radio_request_presets","hp_radio_select_preset","hp_radio_browse_select","hp_radio_browse_back","hp_radio_discover","hp_radio_use_discovered","hp_radio_save_settings"})luaBridgeContract::wrap(L,f,luaBridgeContract::Kind::Action);
}
