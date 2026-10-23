#include "LuaStreamBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "../../services/AppFileStreamService.h"
#include "../../services/SecurityService.h"
#include <algorithm>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {namespace {
void execute(const LuaJobService::Request& r,LuaJobService::Result& out){
    auto& files=AppFileStreamService::instance();
    if((r.cancelled&&r.cancelled->load())||SecurityService::instance().tabletLocked()){out.error="cancelled_or_locked";return;}
    size_t written=0;
    switch(r.method){
    case 0:{const uint32_t id=files.begin(r.owner,r.fileName,r.expected,out.error);out.ok=id!=0;out.body=String(id);
        if(id&&r.cancelled&&r.cancelled->load()){files.abort(r.owner,id);out.ok=false;out.error="cancelled";}break;}
    case 1:out.ok=files.write(r.owner,r.resource,r.offset,reinterpret_cast<const uint8_t*>(r.body.c_str()),r.body.length(),written,out.error);out.processed=written;break;
    case 2:out.ok=files.commit(r.owner,r.resource,written,out.error);out.size=written;out.processed=written;break;
    case 3:out.ok=files.abort(r.owner,r.resource);if(!out.ok)out.error="stream_missing";break;
    case 4:{File file=files.open(r.owner,r.fileName);if(!file||file.isDirectory()){out.error="file_unavailable";break;}
        out.total=file.size();if(r.offset>out.total||!file.seek(r.offset)){out.error="invalid_range";break;}
        char bytes[512];size_t remaining=std::min<size_t>(r.length,out.total-r.offset);
        while(remaining){if((r.cancelled&&r.cancelled->load())||SecurityService::instance().tabletLocked()){out.error="cancelled_or_locked";break;}
            const size_t n=file.read(reinterpret_cast<uint8_t*>(bytes),std::min(remaining,sizeof(bytes)));
            if(!n||!out.body.concat(bytes,n)){out.error="file_read_or_memory";break;}remaining-=n;}
        out.size=out.body.length();out.processed=r.offset+out.size;out.ok=!out.error.length();break;}
    }
}
uint32_t integer(lua_State* L,int index,uint32_t maximum){const lua_Integer n=luaL_checkinteger(L,index);
    if(n<0||static_cast<uint64_t>(n)>maximum)luaL_error(L,"invalid stream integer");return static_cast<uint32_t>(n);}
int queue(lua_State* L,uint8_t method){auto* app=LuaApp::self(L);if(!app||!app->allows(AppPermission::UserStorage))return luaL_error(L,"storage.writeAppData required");
    size_t length=0;const char* name=nullptr;const char* data=nullptr;size_t bytes=0;uint32_t resource=0,offset=0,size=0;
    if(method==0||method==4){name=luaL_checklstring(L,1,&length);if(!length||length>96||strlen(name)!=length)return luaL_error(L,"invalid file name");
        if(method==0)size=integer(L,2,AppFileStreamService::kSdFile);
        else{offset=integer(L,2,AppFileStreamService::kSdFile);size=integer(L,3,AppFileStreamService::kChunk);}}
    else{resource=integer(L,1,0x1fffffff);if(method==1){offset=integer(L,2,AppFileStreamService::kSdFile);data=luaL_checklstring(L,3,&bytes);
            if(bytes>AppFileStreamService::kChunk)return luaL_error(L,"chunk limit exceeded");}}
    LuaJobService::Request r;r.owner=app->id();r.permissions=app->permissions();r.required=perm(AppPermission::UserStorage);
    r.kind=5;r.handler=execute;r.completion="stream.completed";r.method=method;r.resource=resource;r.offset=offset;r.length=size;r.expected=size;
    if(name)r.fileName=String(name,length);if(data)r.body=String(data,bytes);
    if(name&&!AppFileStreamService::validName(r.fileName)){lua_pushnil(L);return 1;}
    const uint32_t id=LuaJobService::instance().submit(r);if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int begin(lua_State* L){return queue(L,0);}int write(lua_State* L){return queue(L,1);}int commit(lua_State* L){return queue(L,2);}
int abort(lua_State* L){return queue(L,3);}int read(lua_State* L){return queue(L,4);}
void cleanup(const char* owner){AppFileStreamService::instance().cancelOwner(owner);}
}
void registerLuaStreamBridge(lua_State* L){LuaJobService::instance().registerOwnerCleanup(cleanup);
    auto* app=LuaApp::self(L);if(!app||!app->allows(AppPermission::UserStorage))return;
    lua_register(L,"hp_stream_begin",begin);lua_register(L,"hp_stream_write",write);lua_register(L,"hp_stream_commit",commit);
    lua_register(L,"hp_stream_abort",abort);lua_register(L,"hp_stream_read",read);}
}
