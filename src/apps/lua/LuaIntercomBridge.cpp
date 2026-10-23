#include "LuaIntercomBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "LuaBridgeContract.h"
#include "../../services/IntercomService.h"
#include "../../services/IntercomAudioService.h"
#include "../../services/VideoService.h"
#include "../../services/AudioHardwareService.h"
#include "../../services/SecurityService.h"
#include <ArduinoJson.h>
#include <cstring>
#include <new>
namespace hp {
    namespace {
        bool access(LuaApp *app, AppPermission permission) {
#if defined(HOMEPANEL_FEATURE_INTERCOM) && !HOMEPANEL_FEATURE_INTERCOM
            if (permission == AppPermission::Intercom) return false;
#endif
            return app && app->allows(permission) && app->allows(AppPermission::Network);
        }
        bool foreground(LuaApp *app) {
            return app &&
                   (app->lifecycle() == LuaRuntimeState::Foreground ||
                    app->lifecycle() == LuaRuntimeState::Opening) &&
                   !SecurityService::instance().tabletLocked();
        }
        void flag(lua_State *L, const char *name, bool value) {
            lua_pushboolean(L, value);
            lua_setfield(L, -2, name);
        }
        void number(lua_State *L, const char *name, uint32_t value) {
            lua_pushinteger(L, value);
            lua_setfield(L, -2, name);
        }
        int capabilities(lua_State *L) {
            auto *app = LuaApp::self(L);
            lua_newtable(L);
            number(L, "version", 1);
            flag(L, "intercom", access(app, AppPermission::Intercom));
            flag(L, "camera", access(app, AppPermission::Camera));
            flag(L, "microphone_permission", app && app->allows(AppPermission::Microphone));
            flag(L, "microphone_ready", AudioHardwareService::instance().microphoneReady());
            flag(L, "speaker_ready", AudioHardwareService::instance().ready());
            flag(L, "door_permission", app && app->allows(AppPermission::DoorOpen));
            flag(L, "rtsp_tcp", true);
            flag(L, "h264_baseline", true);
            flag(L, "jpeg_hardware", true);
            flag(L, "mjpeg_http", true);
            flag(L, "sip", false);
            flag(L, "rtsp_digest", true);
            flag(L, "rtsp_audio", false);
            flag(L, "h265", false);
            number(L, "max_width", 640);
            number(L, "max_height", 480);
            number(L, "max_fps", 10);
            number(L, "encoded_bytes", VideoService::encodedLimit);
            return 1;
        }
        int snapshot(lua_State *L) {
            auto *app = LuaApp::self(L);
            IntercomSnapshot s;
            IntercomDevice d;
            if (!access(app, AppPermission::Intercom) || !IntercomService::instance().snapshot(s) ||
                !IntercomService::instance().device(s.device, d) || strcmp(d.owner, app->id())) {
                lua_pushnil(L);
                return 1;
            }
            lua_newtable(L);
            lua_pushstring(L, intercomStateName(s.state));
            lua_setfield(L, -2, "state");
            number(L, "call", s.call);
            number(L, "revision", s.revision);
            lua_pushstring(L, s.name);
            lua_setfield(L, -2, "name");
            lua_pushstring(L, s.participant);
            lua_setfield(L, -2, "participant");
            lua_pushstring(L, s.error);
            lua_setfield(L, -2, "error");
            flag(L, "muted", s.muted);
            flag(L, "can_answer", s.capabilities.answer);
            flag(L, "can_door", s.capabilities.door && app->allows(AppPermission::DoorOpen));
            return 1;
        }
        int deviceSettings(lua_State *L) {
            auto *app = LuaApp::self(L);
            const char *id = luaL_checkstring(L, 1);
            IntercomDevice d;
            if (!access(app, AppPermission::Intercom) ||
                !IntercomService::instance().device(id, d) || strcmp(d.owner, app->id())) {
                lua_pushnil(L);
                return 1;
            }
            lua_newtable(L);
            for (const auto &item : {std::pair<const char *, const char *>{"id", d.id},
                                     {"provider", d.provider},
                                     {"name", d.name},
                                     {"participant", d.participant},
                                     {"host", d.host},
                                     {"username", d.username}}) {
                lua_pushstring(L, item.second);
                lua_setfield(L, -2, item.first);
            }
            number(L, "port", d.port);
            flag(L, "enabled", d.enabled);
            return 1;
        }
        int command(lua_State *L) {
            auto *app = LuaApp::self(L);
            const char *name = luaL_checkstring(L, 1);
            const auto call = luaL_checkinteger(L, 2);
            IntercomAction action;
            if (!strcmp(name, "answer"))
                action = IntercomAction::Answer;
            else if (!strcmp(name, "hangup"))
                action = IntercomAction::Hangup;
            else if (!strcmp(name, "mute"))
                action = IntercomAction::Mute;
            else if (!strcmp(name, "door"))
                action = IntercomAction::Door;
            else
                return luaL_error(L, "intercom command");
            bool ok = access(app, AppPermission::Intercom) &&
                      app->lifecycle() != LuaRuntimeState::BackgroundExecuting && call > 0 &&
                      call <= UINT32_MAX;
            if (action == IntercomAction::Answer || action == IntercomAction::Mute)
                ok &= app && app->allows(AppPermission::Audio) &&
                      app->allows(AppPermission::Microphone);
            if (action == IntercomAction::Door)
                ok &= app && app->allows(AppPermission::DoorOpen);
            if (action != IntercomAction::Hangup)
                ok &= foreground(app);
            ok = ok && IntercomService::instance().request(
                           app->id(), action, call, lua_toboolean(L, 3), lua_toboolean(L, 4));
            lua_pushboolean(L, ok);
            return 1;
        }
        bool copy(lua_State *L, int table, const char *key, char *out, size_t size,
                  bool required = false) {
            lua_getfield(L, table, key);
            size_t n = 0;
            const char *value = lua_tolstring(L, -1, &n);
            bool ok = !value ? !required : n < size && strlen(value) == n;
            if (ok)
                strlcpy(out, value ? value : "", size);
            lua_pop(L, 1);
            return ok;
        }
        void configureJob(const LuaJobService::Request &request, LuaJobService::Result &result) {
            if (request.cancelled && request.cancelled->load()) {
                result.cancelled = true;
                result.error = "cancelled";
                return;
            }
            JsonDocument input;
            if (deserializeJson(input, request.body)) {
                result.error = "device_json";
                return;
            }
            IntercomDevice d;
            strlcpy(d.owner, request.owner.c_str(), sizeof(d.owner));
            strlcpy(d.id, input["id"] | "", sizeof(d.id));
            strlcpy(d.provider, input["provider"] | "", sizeof(d.provider));
            strlcpy(d.name, input["name"] | "", sizeof(d.name));
            strlcpy(d.participant, input["participant"] | "", sizeof(d.participant));
            strlcpy(d.host, input["host"] | "", sizeof(d.host));
            strlcpy(d.username, input["username"] | "", sizeof(d.username));
            d.port = input["port"] | 80;
            d.enabled = input["enabled"] | false;
            String password = input["password"] | "";
            const bool change = input["password"].is<const char *>();
            result.ok = IntercomService::instance().configure(d, change ? &password : nullptr,
                                                              result.error);
            if (result.ok) {
                result.body = "{\"saved\":true}";
                result.size = result.body.length();
                result.status = 200;
            }
        }
        int configure(lua_State *L) {
            auto *app = LuaApp::self(L);
            if (!access(app, AppPermission::Intercom) || !app->allows(AppPermission::Secrets) ||
                !foreground(app)) {
                lua_pushnil(L);
                return 1;
            }
            luaL_checktype(L, 1, LUA_TTABLE);
            JsonDocument doc;
            char value[257];
            for (const char *key :
                 {"id", "provider", "name", "participant", "host", "username", "password"}) {
                lua_getfield(L, 1, key);
                const bool absent = lua_isnil(L, -1);
                lua_pop(L, 1);
                const size_t limit = !strcmp(key, "id")         ? 33
                                     : !strcmp(key, "provider") ? 25
                                     : !strcmp(key, "host")     ? 97
                                     : !strcmp(key, "password") ? 257
                                                                : 65;
                if (!absent) {
                    if (!copy(L, 1, key, value, limit))
                        return luaL_error(L, "device field limit");
                    doc[key] = value;
                }
            }
            lua_getfield(L, 1, "port");
            const auto port = luaL_optinteger(L, -1, 80);
            lua_pop(L, 1);
            if (port < 1 || port > 65535)
                return luaL_error(L, "device port");
            doc["port"] = port;
            lua_getfield(L, 1, "enabled");
            doc["enabled"] = bool(lua_toboolean(L, -1));
            lua_pop(L, 1);
            LuaJobService::Request job;
            job.owner = app->id();
            job.permissions = app->permissions();
            job.required = perm(AppPermission::Intercom) | perm(AppPermission::Network) |
                           perm(AppPermission::Secrets);
            job.kind = 5;
            job.handler = configureJob;
            serializeJson(doc, job.body);
            const auto id = LuaJobService::instance().submit(job);
            if (id)
                lua_pushinteger(L, id);
            else
                lua_pushnil(L);
            return 1;
        }
        int videoOpen(lua_State *L) {
            auto *app = LuaApp::self(L);
            VideoOptions o;
            String error;
            if (!access(app, AppPermission::Camera) || !foreground(app)) {
                lua_pushnil(L);
                return 1;
            }
            luaL_checktype(L, 1, LUA_TTABLE);
            bool ok = copy(L, 1, "host", o.host, sizeof(o.host), true) &&
                      copy(L, 1, "path", o.path, sizeof(o.path), true) &&
                      copy(L, 1, "username", o.username, sizeof(o.username)) &&
                      copy(L, 1, "secret", o.secretKey, sizeof(o.secretKey));
            auto integer = [&](const char *key, int fallback) {
                lua_getfield(L, 1, key);
                const auto value = luaL_optinteger(L, -1, fallback);
                lua_pop(L, 1);
                return value;
            };
            lua_getfield(L, 1, "transport");
            const char *transport = luaL_optstring(L, -1, "rtsp");
            o.rtsp = !strcmp(transport, "rtsp");
            ok &= o.rtsp || !strcmp(transport, "mjpeg");
            lua_pop(L, 1);
            const auto port = integer("port", o.rtsp ? 554 : 80), w = integer("width", 640),
                       h = integer("height", 480), fps = integer("fps", 5);
            ok &= port > 0 && port <= 65535 && w > 0 && w <= 640 && h > 0 && h <= 480 && fps > 0 &&
                  fps <= 10;
            o.port = port;
            o.width = w;
            o.height = h;
            o.fps = fps;
            if (o.secretKey[0] && !app->allows(AppPermission::Secrets))
                ok = false;
            ok = ok && VideoService::instance().open(app->id(), o, error);
            lua_pushboolean(L, ok);
            if (!ok) {
                lua_pushstring(L, error.length() ? error.c_str() : "video_options");
                return 2;
            }
            return 1;
        }
        int videoStop(lua_State *L) {
            auto *app = LuaApp::self(L);
            if (access(app, AppPermission::Camera))
                VideoService::instance().close(app->id());
            lua_pushboolean(L, access(app, AppPermission::Camera));
            return 1;
        }
        int videoStatus(lua_State *L) {
            auto *app = LuaApp::self(L);
            VideoSnapshot s;
            if (!access(app, AppPermission::Camera) ||
                !VideoService::instance().snapshot(app->id(), s)) {
                lua_pushnil(L);
                return 1;
            }
            lua_newtable(L);
            flag(L, "active", s.active);
            flag(L, "connecting", s.connecting);
            number(L, "revision", s.revision);
            number(L, "frames", s.frames);
            number(L, "dropped", s.dropped);
            number(L, "errors", s.errors);
            lua_pushstring(L, s.error);
            lua_setfield(L, -2, "error");
            return 1;
        }
        struct View {
            LuaApp *app;
            lv_obj_t *object;
            lv_image_dsc_t descriptor{};
            std::shared_ptr<VideoFrame> frame;
        };
        View *views[4]{};
        int viewCreate(lua_State *L) {
            auto *app = LuaApp::self(L);
            auto *parent = static_cast<lv_obj_t *>(lua_touserdata(L, 1));
            if (!access(app, AppPermission::Camera) || !foreground(app) || !parent ||
                !lv_obj_is_valid(parent))
                return luaL_error(L, "video UI unavailable");
            bool owned = false;
            for (auto *p = parent; p; p = lv_obj_get_parent(p))
                if (p == app->screenRoot())
                    owned = true;
            if (!owned)
                return luaL_error(L, "video parent owner");
            View **slot = nullptr;
            for (auto *&v : views)
                if (!v) {
                    slot = &v;
                    break;
                }
            if (!slot || !app->reserveUiObjects(1)) {
                lua_pushnil(L);
                return 1;
            }
            if (!app->reserveNativeBytes(sizeof(View))) {
                app->releaseUiObjects(1);
                lua_pushnil(L);
                return 1;
            }
            auto *view = new (std::nothrow) View{app, lv_image_create(parent)};
            if (!view) {
                app->releaseNativeBytes(sizeof(View));
                app->releaseUiObjects(1);
                lua_pushnil(L);
                return 1;
            }
            *slot = view;
            lv_obj_add_event_cb(
                view->object,
                [](lv_event_t *e) {
                    auto *v = static_cast<View *>(lv_event_get_user_data(e));
                    for (auto *&entry : views)
                        if (entry == v)
                            entry = nullptr;
                    lv_image_cache_drop(&v->descriptor);
                    v->app->releaseUiObjects(1);
                    v->app->releaseNativeBytes(sizeof(View));
                    delete v;
                },
                LV_EVENT_DELETE, view);
            lua_pushlightuserdata(L, view->object);
            return 1;
        }
    } // namespace
    void pollLuaVideoViews() {
        VideoService::instance().uiConsumed();
        for (auto *view : views)
            if (view) {
                const bool visible = foreground(view->app) &&
                                     !lv_obj_has_flag(view->object, LV_OBJ_FLAG_HIDDEN) &&
                                     lv_obj_is_visible(lv_obj_get_parent(view->object));
                auto frame = visible ? VideoService::instance().frame(view->app->id()) : nullptr;
                if (frame == view->frame)
                    continue;
                lv_image_cache_drop(&view->descriptor);
                view->frame = frame;
                if (!frame) {
                    lv_image_set_src(view->object, nullptr);
                    continue;
                }
                auto &image = *frame->image;
                view->descriptor = {};
                view->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
                view->descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
                view->descriptor.header.w = image.width;
                view->descriptor.header.h = image.height;
                view->descriptor.header.stride = image.width * 2;
                view->descriptor.data_size = image.width * image.height * 2;
                view->descriptor.data = image.pixels;
                lv_image_set_src(view->object, &view->descriptor);
                lv_obj_invalidate(view->object);
            }
    }
    void registerLuaIntercomBridge(lua_State *L) {
        lua_register(L, "hp_intercom_capabilities", capabilities);
        lua_register(L, "hp_intercom_snapshot", snapshot);
        lua_register(L, "hp_intercom_device", deviceSettings);
        lua_register(L, "hp_intercom_command", command);
        lua_register(L, "hp_intercom_configure", configure);
        lua_register(L, "hp_video_open", videoOpen);
        lua_register(L, "hp_video_stop", videoStop);
        lua_register(L, "hp_video_snapshot", videoStatus);
        lua_register(L, "hp_video_view", viewCreate);
        for (const char *name : {"hp_intercom_capabilities", "hp_intercom_snapshot",
                                 "hp_intercom_device", "hp_video_snapshot"})
            luaBridgeContract::wrap(L, name, luaBridgeContract::Kind::Query);
        for (const char *name :
             {"hp_intercom_command", "hp_intercom_configure", "hp_video_open", "hp_video_stop"})
            luaBridgeContract::wrap(L, name, luaBridgeContract::Kind::Action);
    }
} // namespace hp
