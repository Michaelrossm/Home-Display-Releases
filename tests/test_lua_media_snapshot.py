from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
service_h = (ROOT / "src/services/MediaSessionService.h").read_text(encoding="utf-8")
service_cpp = (ROOT / "src/services/MediaSessionService.cpp").read_text(encoding="utf-8")
lua_cpp = (ROOT / "src/apps/lua/LuaMediaBridge.cpp").read_text(encoding="utf-8")

assert "bool trySnapshot(MediaState& out);" in service_h
body = service_cpp.split("bool MediaSessionService::trySnapshot", 1)[1].split(
    "MediaPresentationState MediaSessionService::presentation", 1
)[0]
assert "xSemaphoreTakeRecursive(lock_, 0)" in body
assert "MediaSessionService::instance().trySnapshot(s);" in lua_cpp
assert "MediaSessionService::instance().snapshot();" not in lua_cpp.split(
    "int bridgeNowPlaying", 1
)[1].split("int bridgeMusicSnapshot", 1)[0]
print("PASS Lua media snapshots never wait for the media lock")
