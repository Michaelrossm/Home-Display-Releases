#include "LuaSecurityBridge.h"
#include "../../services/SecurityService.h"
#include "../../ui/SecurityUi.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
int status(lua_State* L){auto& s=SecurityService::instance();lua_newtable(L);lua_pushboolean(L,s.enabled(PinKind::Settings));lua_setfield(L,-2,"enabled");lua_pushboolean(L,s.settingsAllowed());lua_setfield(L,-2,"unlocked");lua_pushinteger(L,s.blockedSeconds(PinKind::Settings));lua_setfield(L,-2,"blocked_seconds");return 1;}
int unlock(lua_State* L){auto& s=SecurityService::instance();size_t size=0;const char* pin=luaL_checklstring(L,1,&size);lua_pushboolean(L,!s.tabletLocked()&&size<=12&&s.unlock(PinKind::Settings,String(pin,size)));return 1;}
int lock(lua_State*){SecurityService::instance().lockSettings();return 0;}
int prompt(lua_State*){if(!SecurityService::instance().tabletLocked())SecurityUi::requestSettingsUnlock();return 0;}
}
void registerLuaSecurityBridge(lua_State* L){lua_register(L,"hp_security_settings_status",status);lua_register(L,"hp_security_unlock_settings",unlock);lua_register(L,"hp_security_lock_settings",lock);lua_register(L,"hp_security_request_unlock_settings",prompt);}
}
