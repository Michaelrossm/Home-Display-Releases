#include "PrinterLuaBridge.h"
#include "LuaApp.h"
#include "LuaBackgroundRun.h"
#include "LuaJsonBridge.h"
#include "LuaJobService.h"
#include "LuaBridgeContract.h"
#include "../../services/PrinterService.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp { namespace {
LuaApp* allowed(lua_State* L){auto* app=LuaApp::self(L);return app&&app->allows(AppPermission::Network)&&app->allows(AppPermission::UserStorage)?app:nullptr;}
int fail(lua_State* L,const char* error){return luaBridgeContract::fail(L,error);}
bool idArgument(lua_State* L,int index,const char*& id){size_t n=0;id=lua_tolstring(L,index,&n);
    return id&&strlen(id)==n&&printerIdentity(id,36);}
int push(lua_State* L,const String& json){return pushLuaJsonBounded(L,json.c_str(),json.length(),2048);}
int list(lua_State* L){auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    auto& s=PrinterService::instance();if(!s.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    String error;const String json=s.profiles(app->id(),&error,lua_toboolean(L,1));
    if(!json.length())return fail(L,error.length()?error.c_str():"printer_profiles_loading");return push(L,json);}
int profilesState(lua_State* L){auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    auto& service=PrinterService::instance();
    if(!service.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    return push(L,service.profileState(app->id()));
}
int revision(lua_State* L){const char* id=nullptr;auto* app=allowed(L);uint32_t rev=0,stamp=0;bool pending=false,online=false;
    if(!app||!idArgument(L,1,id)||!PrinterService::instance().snapshotRevision(app->id(),id,rev,stamp,pending,online)){lua_pushnil(L);return 1;}
    lua_pushnumber(L,rev);lua_pushnumber(L,stamp);lua_pushboolean(L,pending);lua_pushboolean(L,online);return 4;
}
int snapshot(lua_State* L){const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");auto& s=PrinterService::instance();
    if(!s.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    // Legacy table decoder may exceed the 12-ms Lua tick budget on schema-2
    // snapshots with many temperature/tool components. Reject oversized legacy
    // decoding explicitly instead of quarantining the entire app.
    const String data=s.snapshot(app->id(),id,app->lifecycle()==LuaRuntimeState::Foreground&&!luaBackgroundActive(L));
    if(data.length()>8192)return fail(L,"printer_snapshot_use_sections");
    return push(L,data);
}
int watch(lua_State* L){
    const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    auto& service=PrinterService::instance();
    if(!service.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    lua_pushboolean(L,service.watch(app->id(),id,lua_toboolean(L,2)));return 1;
}
// Raw JSON avoids recursively constructing hundreds of Lua objects inside
// the LVGL tick. The source is already cached by the asynchronous worker.
int snapshotRaw(lua_State* L){const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");auto& s=PrinterService::instance();
    if(!s.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    const String data=s.snapshot(app->id(),id,app->lifecycle()==LuaRuntimeState::Foreground&&!luaBackgroundActive(L));
    if(data.length()>32768)return fail(L,"printer_snapshot_limit");
    lua_pushlstring(L,data.c_str(),data.length());return 1;
}
// The section reader scans the already cached JSON without allocating a
// second large ArduinoJson document. It only acVepts documented array keys.
static bool sectionName(const char* n){return n&&(!strcmp(n,"temperatures")||!strcmp(n,"heaters")||
  !strcmp(n,"fans")||!strcmp(n,"axes")||!strcmp(n,"tools")||!strcmp(n,"extruders")||!strcmp(n,"actions"));}
static size_t jsonValueEnd(const String& s,size_t start){
    if(start>=s.length())return static_cast<size_t>(-1);
    const char first=s[start];
    if(first!='{'&&first!='['){
        if(first=='"'){bool escaped=false;for(size_t i=start+1;i<s.length();++i){const char c=s[i];
            if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='"')return i+1;}return static_cast<size_t>(-1);}
        size_t i=start;while(i<s.length()&&s[i]!=','&&s[i]!=']'&&s[i]!='}'&&s[i]!=' ')++i;return i;
    }
    char stack[20];unsigned depth=0;bool quoted=false,escaped=false;
    for(size_t i=start;i<s.length();++i){const char c=s[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;continue;}
        if(c=='{'||c=='['){if(depth>=sizeof(stack))return static_cast<size_t>(-1);stack[depth++]=c;continue;}
        if(c=='}'||c==']'){if(!depth)return static_cast<size_t>(-1);const char matching= c=='}'?'{':'[';
            if(stack[--depth]!=matching)return static_cast<size_t>(-1);if(!depth)return i+1;}
    }
    return static_cast<size_t>(-1);
}
static bool findTopArray(const String& raw,const char* key,size_t& from,size_t& to){
    // PrinterService emits a flat top-level JSON object. Ignore matches
    // nested inside strings and other objects; no untrusted path expressions.
    if(!raw.length()||raw[0]!='{')return false;
    size_t p=1;
    while(p<raw.length()){
        while(p<raw.length()&&(raw[p]==' '||raw[p]==','))++p;
        if(p>=raw.length()||raw[p]=='}')break;
        if(raw[p]!='"')return false;
        const size_t keyEnd=jsonValueEnd(raw,p);if(keyEnd==static_cast<size_t>(-1))return false;
        const bool correct=keyEnd-p==strlen(key)+2&&raw.substring(p+1,keyEnd-1)==key;
        p=keyEnd;while(p<raw.length()&&raw[p]==' ')++p;
        if(p>=raw.length()||raw[p]!=':')return false;++p;
        while(p<raw.length()&&raw[p]==' ')++p;
        if(p>=raw.length())return false;
        const size_t valueEnd=jsonValueEnd(raw,p);if(valueEnd==static_cast<size_t>(-1))return false;
        if(correct&&raw[p]=='['){from=p+1;to=valueEnd-1;return true;}
        p=valueEnd;
    }
    return false;
}
int snapshotSection(lua_State* L){
    const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    size_t length=0;const char* name=lua_tolstring(L,2,&length);
    if(!name||strlen(name)!=length||!sectionName(name))return fail(L,"printer_argument_section");
    const lua_Integer offset=luaL_optinteger(L,3,0), count=luaL_optinteger(L,4,4);
    if(offset<0||offset>256||count<1||count>8)return fail(L,"printer_argument_range");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");auto& service=PrinterService::instance();
    if(!service.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    const String data=service.snapshot(app->id(),id,app->lifecycle()==LuaRuntimeState::Foreground&&!luaBackgroundActive(L));
    size_t begin=0,end=0;if(!findTopArray(data,name,begin,end))return fail(L,"printer_section_unavailable");
    String page="[";unsigned selected=0;size_t index=0,position=begin;
    while(position<end){while(position<end&&(data[position]==' '||data[position]==','))++position;
        if(position>=end)break;
        const size_t itemEnd=jsonValueEnd(data,position);if(itemEnd==static_cast<size_t>(-1)||itemEnd>end)return fail(L,"printer_invalid_model_or_limit");
        if(index>=static_cast<size_t>(offset)&&selected<static_cast<unsigned>(count)){
            if(selected++)page+=',';
            page+=data.substring(position,itemEnd);
        }
        ++index;position=itemEnd;
        if(selected>=static_cast<unsigned>(count))break;
    }
    page+=']';
    if(page.length()>8192)return fail(L,"printer_section_limit");
    return push(L,page);
}
// A compact overview keeps the UI responsive even if the native snapshot
// contains many heaters, sensors and tools. No JSON document is decoded here.
static bool coreField(const String& name){
    for(const char* key:{"online","pending","stale","error","message","status","state",
          "revision","timestamp","profile_id","filename","progress","duration",
          "remaining","layer","layers","api","speed_factor","flow_factor",
          "schema_version","truncated","controls","operation"})if(name==key)return true;
    return false;
}
int snapshotSummary(lua_State* L){
    const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    auto& service=PrinterService::instance();
    if(!service.arm(app->id(),app->permissions()))return fail(L,"printer_busy");
    const String data=service.snapshot(app->id(),id,app->lifecycle()==LuaRuntimeState::Foreground&&!luaBackgroundActive(L));
    if(data.length()>32768||!data.startsWith("{"))return fail(L,"printer_snapshot_limit");
    String summary="{";size_t p=1;unsigned fields=0;
    while(p<data.length()){
        while(p<data.length()&&(data[p]==','||data[p]==' '))++p;
        if(p>=data.length()||data[p]=='}')break;
        if(data[p]!='"')return fail(L,"printer_invalid_model_or_limit");
        size_t keyEnd=jsonValueEnd(data,p);
        if(keyEnd==static_cast<size_t>(-1))return fail(L,"printer_invalid_model_or_limit");
        String name=data.substring(p+1,keyEnd-1);const size_t start=p;
        p=keyEnd;while(p<data.length()&&data[p]==' ')++p;
        if(p>=data.length()||data[p++]!=':')return fail(L,"printer_invalid_model_or_limit");
        while(p<data.length()&&data[p]==' ')++p;
        size_t valueEnd=jsonValueEnd(data,p);
        if(valueEnd==static_cast<size_t>(-1))return fail(L,"printer_invalid_model_or_limit");
        if(coreField(name)){
            if(fields++)summary+=',';
            summary+=data.substring(start,valueEnd);
        }
        p=valueEnd;
    }
    summary+='}';if(summary.length()>4096)return fail(L,"printer_summary_limit");
    return push(L,summary);
}
int directory(lua_State* L,bool macros){const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    size_t n=0;const char* raw=lua_isnoneornil(L,2)?"/":lua_tolstring(L,2,&n);
    if(!raw||n>160||strlen(raw)!=(n?n:1))return fail(L,"printer_path_invalid");
    const lua_Integer offset=luaL_optinteger(L,4,0);
    if(offset<0||offset>1024)return fail(L,"printer_argument_range");
    return push(L,PrinterService::instance().files(app->id(),id,String(raw),macros,
        lua_toboolean(L,3),static_cast<uint16_t>(offset)));}
int files(lua_State* L){return directory(L,false);}int macros(lua_State* L){return directory(L,true);}
int actionCatalog(lua_State* L){const int offset=luaL_optinteger(L,1,0),count=luaL_optinteger(L,2,4);if(offset<0||offset>32||count<1||count>4)return fail(L,"printer_argument_range");
    JsonDocument d;printerWriteActionCatalog(d.to<JsonArray>(),offset,count);String json;serializeJson(d,json);return push(L,json);}
int operation(lua_State* L){const char* id=nullptr;auto* app=allowed(L);if(!app||!idArgument(L,1,id))return fail(L,"printer_permission");return push(L,PrinterService::instance().operation(app->id(),id));}
int temperatureHistory(lua_State* L){const char* id=nullptr;auto* app=allowed(L);if(!app||!idArgument(L,1,id))return fail(L,"printer_permission");
    size_t size=0;const char* sensor=lua_tolstring(L,2,&size);const int offset=luaL_optinteger(L,3,0),count=luaL_optinteger(L,4,8);
    if(!sensor||size!=strlen(sensor)||size>63||offset<0||offset>=60||count<1||count>16)return fail(L,"printer_argument_range");
    return push(L,PrinterService::instance().history(app->id(),id,sensor,offset,count));}
int console(lua_State* L){const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");return push(L,PrinterService::instance().console(app->id(),id));}
int command(lua_State* L){
    const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    size_t n=0;const char* kind=lua_tolstring(L,2,&n);
    if(!kind||strlen(kind)!=n||n>32||!n||!lua_isnoneornil(L,3)&&!lua_istable(L,3))return fail(L,"printer_argument_command");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    if(luaBackgroundActive(L)||app->lifecycle()==LuaRuntimeState::BackgroundExecuting)return fail(L,"printer_background_control_denied");
    String body="{}";const bool valid=lua_isnoneornil(L,3)||encodeLuaJson(L,3,body);
    const uint32_t job=valid?PrinterService::instance().command(app->id(),app->permissions(),id,kind,body):0;
    return luaBridgeContract::job(L,job,valid?"printer_profile_or_job_unavailable":"printer_argument_invalid");
}
int capabilities(lua_State* L){lua_newtable(L);lua_pushinteger(L,2);lua_setfield(L,-2,"version");
    lua_pushinteger(L,kPrinterSchemaVersion);lua_setfield(L,-2,"schema_version");
    lua_pushinteger(L,PrinterStatus::kTemperatures);lua_setfield(L,-2,"max_temperatures");
    lua_pushboolean(L,true);lua_setfield(L,-2,"components");
    lua_pushboolean(L,true);lua_setfield(L,-2,"typed_controls");
    lua_pushboolean(L,true);lua_setfield(L,-2,"profile_controls");
    lua_pushboolean(L,true);lua_setfield(L,-2,"action_catalog");lua_pushboolean(L,true);lua_setfield(L,-2,"temperature_history");
    lua_pushboolean(L,true);lua_setfield(L,-2,"section_paging");
    lua_pushboolean(L,true);lua_setfield(L,-2,"summary_snapshot");
    lua_pushboolean(L,true);lua_setfield(L,-2,"raw_snapshot");
    lua_pushboolean(L,true);lua_setfield(L,-2,"reprap_standalone");lua_pushboolean(L,true);lua_setfield(L,-2,"reprap_dsf");
    lua_pushboolean(L,true);lua_setfield(L,-2,"moonraker");
    lua_pushboolean(L,true);lua_setfield(L,-2,"octoprint");
    lua_pushboolean(L,true);lua_setfield(L,-2,"file_paging");
    lua_pushboolean(L,true);lua_setfield(L,-2,"file_upload");
    lua_pushboolean(L,true);lua_setfield(L,-2,"file_preview");
    lua_pushinteger(L,kPrinterCommandQueueLimitMs);lua_setfield(L,-2,"command_queue_limit_ms");
    lua_pushboolean(L,false);lua_setfield(L,-2,"automatic_queue_start");
    for(const char* name:{"camera","history"}){lua_pushboolean(L,false);lua_setfield(L,-2,name);}
    lua_pushinteger(L,PrinterService::kProfiles);lua_setfield(L,-2,"profiles");return 1;}
int fileAction(lua_State* L,bool upload){
    const char* id=nullptr;if(!idArgument(L,1,id))return fail(L,"printer_argument_profile");
    auto* app=allowed(L);if(!app)return fail(L,"printer_permission");
    if(luaBackgroundActive(L)||app->lifecycle()!=LuaRuntimeState::Foreground)return fail(L,"printer_background_control_denied");
    if(!lua_istable(L,2))return fail(L,"printer_argument_invalid");
    String body;if(!encodeLuaJson(L,2,body)||body.length()>1024)return fail(L,"printer_argument_invalid");
    const auto job=PrinterService::instance().command(app->id(),app->permissions(),id,upload?"upload_file":"preview_file",body);
    return luaBridgeContract::job(L,job,"printer_profile_or_job_unavailable");
}
int upload(lua_State* L){return fileAction(L,true);}int preview(lua_State* L){return fileAction(L,false);}
}
void registerPrinterLuaBridge(lua_State* L){
    LuaJobService::instance().registerOwnerCleanup([](const char* owner){PrinterService::instance().cleanupOwner(owner,LuaJobService::instance().ownerSuspending());});
    lua_register(L,"hp_printer_list",list);lua_register(L,"hp_printer_profile_state",profilesState);lua_register(L,"hp_printer_snapshot",snapshot);
    lua_register(L,"hp_printer_snapshot_raw",snapshotRaw);
    lua_register(L,"hp_printer_snapshot_summary",snapshotSummary);
    lua_register(L,"hp_printer_revision",revision);
    lua_register(L,"hp_printer_watch",watch);
    lua_register(L,"hp_printer_snapshot_section",snapshotSection);
    lua_register(L,"hp_printer_command",command);lua_register(L,"hp_printer_files",files);
    lua_register(L,"hp_printer_macros",macros);lua_register(L,"hp_printer_console",console);
    lua_register(L,"hp_printer_capabilities",capabilities);
    lua_register(L,"hp_printer_action_catalog",actionCatalog);lua_register(L,"hp_printer_temperature_history",temperatureHistory);
    lua_register(L,"hp_printer_operation",operation);
    lua_register(L,"hp_printer_upload",upload);lua_register(L,"hp_printer_preview",preview);
    luaBridgeContract::registerCapabilities(L);
    for (const char* name : {"hp_printer_list", "hp_printer_profile_state", "hp_printer_snapshot", "hp_printer_snapshot_raw",
        "hp_printer_snapshot_summary", "hp_printer_snapshot_section", "hp_printer_files",
        "hp_printer_macros", "hp_printer_console", "hp_printer_capabilities","hp_printer_action_catalog","hp_printer_temperature_history","hp_printer_operation"})
        luaBridgeContract::wrap(L, name, luaBridgeContract::Kind::Query);
    luaBridgeContract::wrap(L, "hp_printer_command", luaBridgeContract::Kind::Action);
    luaBridgeContract::wrap(L, "hp_printer_upload", luaBridgeContract::Kind::Action);
    luaBridgeContract::wrap(L, "hp_printer_preview", luaBridgeContract::Kind::Action);
}
}
