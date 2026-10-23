#include "LuaMediaBridge.h"
#include "LuaBridgeContract.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include "LuaApp.h"
#include "LuaJobService.h"
#include "../../services/MediaSessionService.h"
#include "../music/MusicPlayerService.h"
#include <cstring>
namespace hp { namespace {
MediaCommand parseCommand(const char* s){
 if(!s)return MediaCommand::PlayPause;
 if(!strcmp(s,"stop"))return MediaCommand::Stop;
 if(!strcmp(s,"next"))return MediaCommand::Next;
 if(!strcmp(s,"previous"))return MediaCommand::Previous;
 if(!strcmp(s,"power_toggle"))return MediaCommand::PowerToggle;
 if(!strcmp(s,"mute_toggle"))return MediaCommand::MuteToggle;
 if(!strcmp(s,"volume_up"))return MediaCommand::VolumeUp;
 if(!strcmp(s,"volume_down"))return MediaCommand::VolumeDown;
 return MediaCommand::PlayPause;
}
int bridgeMediaCommand(lua_State* L){
 auto* app=LuaApp::self(L);LuaJobService::Request request;request.kind=4;
 request.method=static_cast<uint8_t>(parseCommand(luaL_optstring(L,1,"play_pause")));
 if(app){request.owner=app->id();request.permissions=app->permissions();}
 const uint32_t id=app?LuaJobService::instance().submit(request):0;
 if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int bridgeNowPlaying(lua_State* L){
 MediaState s;const bool valid=MediaSessionService::instance().trySnapshot(s);
 lua_newtable(L);lua_pushboolean(L,valid);lua_setfield(L,-2,"valid");
 lua_pushstring(L,s.title.c_str());lua_setfield(L,-2,"title");
 lua_pushstring(L,s.subtitle.c_str());lua_setfield(L,-2,"subtitle");
 lua_pushboolean(L,s.playing);lua_setfield(L,-2,"playing");
 lua_pushinteger(L,s.volume);lua_setfield(L,-2,"volume");return 1;
}
int bridgeMusicSnapshot(lua_State* L){
 const auto s=MusicPlayerService::instance().snapshot();lua_newtable(L);
 lua_pushstring(L,s.title);lua_setfield(L,-2,"title");lua_pushinteger(L,s.count);lua_setfield(L,-2,"count");
 lua_pushinteger(L,s.volume);lua_setfield(L,-2,"volume");lua_pushstring(L,s.error);lua_setfield(L,-2,"error");return 1;
}
int bridgeMusicPlayIndex(lua_State* L){
 const lua_Integer index=luaL_optinteger(L,1,0);const auto state=MusicPlayerService::instance().snapshot();
 const bool valid=index>=0&&index<state.count;
 if(valid)MusicPlayerService::instance().playIndex((size_t)index);
 lua_pushboolean(L,valid);return 1;
}
}}
void hp::registerLuaMediaBridge(lua_State* L){
 auto* app=LuaApp::self(L);if(!app||!app->allows(AppPermission::Audio))return;
 lua_register(L,"hp_media_command",bridgeMediaCommand);
 lua_register(L,"hp_now_playing",bridgeNowPlaying);
 lua_register(L,"hp_music_snapshot",bridgeMusicSnapshot);
 lua_register(L,"hp_music_play_index",bridgeMusicPlayIndex);
 luaBridgeContract::registerCapabilities(L);
 luaBridgeContract::wrap(L,"hp_media_command",luaBridgeContract::Kind::Action);
 luaBridgeContract::wrap(L,"hp_now_playing",luaBridgeContract::Kind::Query);
 luaBridgeContract::wrap(L,"hp_music_snapshot",luaBridgeContract::Kind::Query);
 luaBridgeContract::wrap(L,"hp_music_play_index",luaBridgeContract::Kind::Action);
}
