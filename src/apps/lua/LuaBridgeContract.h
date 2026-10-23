#pragma once
// Unified Lua API v2: a consistent envelope for native bridge calls.
// This does not create a new worker; it wraps each existing service's result.
#include <Arduino.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp { namespace luaBridgeContract {
constexpr int kVersion = 2;
enum class Kind : int { Query = 0, Action = 1 };
inline void str(lua_State* L, const char* key, const char* value) {
    lua_pushstring(L, value ? value : ""); lua_setfield(L, -2, key);
}
inline void integer(lua_State* L, const char* key, lua_Integer n) {
    lua_pushinteger(L, n); lua_setfield(L, -2, key);
}
inline void boolean(lua_State* L, const char* key, bool b) {
    lua_pushboolean(L, b); lua_setfield(L, -2, key);
}
inline int invoke(lua_State* L) {
    const int nargs = lua_gettop(L);
    const Kind kind = static_cast<Kind>(lua_tointeger(L, lua_upvalueindex(2)));
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    const int rc = lua_pcall(L, nargs, LUA_MULTRET, 0);
    // A Lua error is represented as an API result rather than aborting callers.
    if (rc != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        lua_newtable(L);
        boolean(L, "ok", false); boolean(L, "valid", false);
        boolean(L, "pending", false);
        str(L, "error_code", "bridge_callback_error");
        str(L, "error", error ? error : "bridge_callback_error");
        integer(L, "api_version", kVersion);
        integer(L, "timestamp_ms", millis());
        return 1;
    }
    const int count = lua_gettop(L);
    const int type = count ? lua_type(L, 1) : LUA_TNONE;
    bool ok = false, valid = false, pending = false;
    lua_Integer jobId = 0;
    String error;
    if (type == LUA_TTABLE) {
        ok = true; valid = true;
        lua_getfield(L, 1, "valid");
        if (lua_isboolean(L, -1)) valid = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "error");
        if (lua_isstring(L, -1)) error = lua_tostring(L, -1);
        lua_pop(L, 1);
    } else if (type == LUA_TBOOLEAN) {
        // Queries may validly return false (e.g. catalog_busy=false).
        // A false action result instead denotes a rejected operation.
        ok = kind == Kind::Query || lua_toboolean(L, 1);
        valid = ok;
        if (!ok) error = "operation_failed";
    } else if (type == LUA_TNUMBER && kind == Kind::Action && lua_isinteger(L, 1)) {
        jobId = lua_tointeger(L, 1);
        ok = jobId > 0; valid = ok; pending = ok;
        if (!ok) error = "job_unavailable";
    } else if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        ok = true; valid = true;
    } else if (type == LUA_TNONE && kind == Kind::Action) {
        // Legacy action with no return value: accepted by the synchronous call.
        ok = true; valid = true;
    } else {
        error = count >= 2 && lua_isstring(L, 2) ? lua_tostring(L, 2) : "data_unavailable";
    }
    lua_newtable(L);
    const int result = lua_gettop(L);
    boolean(L, "ok", ok);
    boolean(L, "valid", valid);
    boolean(L, "pending", pending);
    integer(L, "api_version", kVersion);
    integer(L, "timestamp_ms", millis());
    if (jobId > 0) integer(L, "job_id", jobId);
    if (error.length()) {str(L, "error_code", error.c_str());str(L, "error", error.c_str());}
    else {lua_pushnil(L); lua_setfield(L, result, "error_code");}
    if (count > 0 && type != LUA_TNONE && type != LUA_TNIL) {
        lua_pushvalue(L, 1);
        lua_setfield(L, result, "data");
    }
    // Explicit synchronous/asynchronous semantics; never invent job IDs.
    str(L, "execution", pending ? "queued" : (kind == Kind::Action ? "synchronous" : "cached"));
    return 1;
}
// Older internal C++ bridge helpers remain source-compatible. Lua exports use v2 envelopes.
inline int fail(lua_State* L, const char* code) {
    lua_pushnil(L); lua_pushstring(L, code ? code : "internal_error"); return 2;
}
inline int job(lua_State* L, uint32_t id, const char* code) {
    if (!id) return fail(L, code ? code : "job_unavailable");
    lua_pushinteger(L, id); return 1;
}
inline bool wrap(lua_State* L, const char* name, Kind kind) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushinteger(L, static_cast<int>(kind));
    lua_pushcclosure(L, invoke, 2);
    lua_setglobal(L, name);
    return true;
}
inline int capabilities(lua_State* L) {
    lua_newtable(L);
    integer(L, "api_version", kVersion);
    str(L, "result", "table_envelope");
    str(L, "errors", "error_code");
    str(L, "async", "pending_job_id");
    str(L, "timestamps", "timestamp_ms");
    boolean(L, "permission_checks", true);
    boolean(L, "shared_worker", true);
    return 1;
}
inline void registerCapabilities(lua_State* L) {
    lua_register(L, "hp_bridge_contract", capabilities);
}
} } // namespace hp::luaBridgeContract
