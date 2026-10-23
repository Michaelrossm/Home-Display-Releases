#include "IntercomService.h"
#include "IntercomAudioService.h"
#include "VideoService.h"
#include "ConfigService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include "IdleService.h"
#include "../core/EventBus.h"
#include "../core/AppRegistry.h"
#include "../apps/lua/LuaJobService.h"
#include "../ui/UiTheme.h"
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>
#include <esp_heap_caps.h>
namespace hp {
    namespace {
        bool token(const char *value, size_t cap) {
            if (!value || !*value || strnlen(value, cap) >= cap)
                return false;
            for (const char *p = value; *p; ++p)
                if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.')
                    return false;
            return true;
        }
        class LanEventProvider final : public IntercomProvider {
          public:
            const char *id() const override { return "lan_event"; }
            IntercomCapabilities capabilities(const IntercomDevice &) const override {
                IntercomCapabilities c;
                c.ringEvents = true;
                return c;
            }
            bool command(const IntercomDevice &, IntercomAction, bool, const String &,
                         String &e) override {
                e = "provider_action_unsupported";
                return false;
            }
        } lanEvent;
        // Video adapters only. No inferred ring/door/audio support from an HTTP port.
        class CameraProvider final : public IntercomProvider {
            const char *name_;
          public:
            explicit CameraProvider(const char *name):name_(name){}
            const char *id() const override { return name_; }
            IntercomCapabilities capabilities(const IntercomDevice &) const override {
                IntercomCapabilities c; c.video=true; return c;
            }
            bool command(const IntercomDevice &, IntercomAction, bool, const String &,
                         String &error) override { error="provider_action_unsupported";return false; }
        } dahuaCamera("dahua"), hikvisionCamera("hikvision"), doorbirdCamera("doorbird");
        struct CallOverlay {
            lv_obj_t *root = nullptr, *label = nullptr, *answer = nullptr, *door = nullptr,
                     *mute = nullptr;
            uint32_t call = 0, doorConfirmCall = 0, doorUntil = 0;
        } overlay;
        void action(lv_event_t *event) {
            const auto a = static_cast<IntercomAction>(
                reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
            if (a == IntercomAction::Door) {
                if (overlay.doorConfirmCall != overlay.call ||
                    int32_t(millis() - overlay.doorUntil) >= 0) {
                    overlay.doorConfirmCall = overlay.call;
                    overlay.doorUntil = millis() + 5000;
                    lv_label_set_text(lv_obj_get_child(overlay.door, 0), "Bestaetigen");
                    return;
                }
                overlay.doorConfirmCall = 0;
                lv_label_set_text(lv_obj_get_child(overlay.door, 0), "Tuer oeffnen");
            }
            IntercomSnapshot state;
            const bool value = a == IntercomAction::Mute &&
                               IntercomService::instance().snapshot(state) && !state.muted;
            IntercomService::instance().request(nullptr, a, overlay.call, value,
                                                a == IntercomAction::Door);
        }
        lv_obj_t *button(lv_obj_t *parent, const char *text, IntercomAction a) {
            auto *b = lv_button_create(parent);
            auto *l = lv_label_create(b);
            lv_label_set_text(l, text);
            lv_obj_center(l);
            lv_obj_set_size(b, 108, 54);
            lv_obj_add_event_cb(b, action, LV_EVENT_CLICKED,
                                reinterpret_cast<void *>(uintptr_t(a)));
            return b;
        }
    } // namespace
    const char *intercomStateName(IntercomState s) {
        static const char *names[] = {"offline",      "ready", "ringing", "connecting",
                                      "conversation", "ended", "error"};
        return names[unsigned(s) <= 6 ? unsigned(s) : 0];
    }
    IntercomService &IntercomService::instance() {
        static IntercomService s;
        return s;
    }
    bool IntercomService::begin() {
        if (mutex_)
            return true;
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_)
            return false;
        registerProvider(&lanEvent);
        registerProvider(&dahuaCamera);
        registerProvider(&hikvisionCamera);
        registerProvider(&doorbirdCamera);
        LuaJobService::instance().registerOwnerCleanup([](const char *owner) {
            if (!LuaJobService::instance().ownerSuspending())
                IntercomService::instance().detachOwner(owner);
        });
        JsonDocument doc;
        const auto saved = ConfigService::instance().getString("ic_devices", "[]");
        if (saved.length() > 4096 || deserializeJson(doc, saved) || !doc.is<JsonArray>() ||
            doc.size() > 4) {
            diagnostic("config_invalid");
            return true;
        }
        unsigned i = 0;
        for (JsonObject row : doc.as<JsonArray>()) {
            auto &d = devices_[i++];
            strlcpy(d.id, row["id"] | "", sizeof(d.id));
            strlcpy(d.owner, row["owner"] | "", sizeof(d.owner));
            strlcpy(d.provider, row["provider"] | "", sizeof(d.provider));
            strlcpy(d.name, row["name"] | "", sizeof(d.name));
            strlcpy(d.participant, row["participant"] | "", sizeof(d.participant));
            strlcpy(d.host, row["host"] | "", sizeof(d.host));
            strlcpy(d.username, row["username"] | "", sizeof(d.username));
            d.port = row["port"] | 80;
            d.videoPort = row["videoPort"] | 554;
            strlcpy(d.videoPath,row["videoPath"] | "",sizeof(d.videoPath));
            d.fps = row["fps"] | 5;
            d.httpVideo = row["httpVideo"] | false;
            d.ringTone = row["ringTone"] | true;
            d.enabled = row["enabled"] | false;
            if (!token(d.id, sizeof(d.id)) || !token(d.owner, sizeof(d.owner)) ||
                !token(d.provider, sizeof(d.provider)) || !d.port)
                d.enabled = false;
        }
        return true;
    }
    IntercomProvider *IntercomService::provider(const char *id) const {
        for (auto *p : providers_)
            if (p && !strcmp(p->id(), id))
                return p;
        return nullptr;
    }
    bool IntercomService::registerProvider(IntercomProvider *p) {
        if (!p || !token(p->id(), 25) || !mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        bool ok = false;
        for (auto *&entry : providers_) {
            if (entry && !strcmp(entry->id(), p->id())) {
                ok = entry == p;
                break;
            }
            if (!entry) {
                entry = p;
                ok = true;
                break;
            }
        }
        xSemaphoreGive(mutex_);
        return ok;
    }
    bool IntercomService::configure(const IntercomDevice &d, const String *password,
                                    String &error) {
        if (!NetworkWorker::instance().isWorkerContext()) {
            error = "worker_required";
            return false;
        }
        SecurityWriteGuard guard;
        if (SecurityService::instance().tabletLocked() ||
            !SecurityService::instance().settingsAllowed()) {
            error = "pin_locked";
            return false;
        }
        if (!token(d.id, sizeof(d.id)) || !token(d.owner, sizeof(d.owner)) ||
            !token(d.provider, sizeof(d.provider)) || !d.port ||
            !memchr(d.host, 0, sizeof(d.host)) || !memchr(d.username, 0, sizeof(d.username)) ||
            !memchr(d.name, 0, sizeof(d.name)) ||
            !memchr(d.participant, 0, sizeof(d.participant)) ||
            !memchr(d.videoPath, 0, sizeof(d.videoPath)) || !d.videoPort || !d.fps || d.fps>10 ||
            (password && password->length() > 256)) {
            error = "device_invalid";
            return false;
        }
        for (const char *p = d.host; *p; ++p)
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') {
                error = "host_invalid";
                return false;
            }
        if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
            error = "busy";
            return false;
        }
        if (!provider(d.provider)) {
            xSemaphoreGive(mutex_);
            error = "provider_missing";
            return false;
        }
        int slot = -1;
        for (unsigned i = 0; i < 4; ++i) {
            if (!strcmp(devices_[i].id, d.id)) {
                if (strcmp(devices_[i].owner, d.owner)) {
                    xSemaphoreGive(mutex_);
                    error = "owner_mismatch";
                    return false;
                }
                slot = i;
                break;
            }
            if (slot < 0 && !devices_[i].id[0])
                slot = i;
        }
        if (slot < 0) {
            xSemaphoreGive(mutex_);
            error = "device_limit";
            return false;
        }
        const auto before = devices_[slot];
        char secretKey[12];
        snprintf(secretKey, sizeof(secretKey), "ic_pass%d", slot);
        auto &config = ConfigService::instance();
        String old;
        // A newly occupied slot never inherits another device's leftover secret.
        const String emptyPassword;
        if(!before.id[0]&&!password)password=&emptyPassword;
        if (password) {
            old = config.getSecretString(secretKey);
            if (!config.putSecretString(secretKey, *password)) {
                xSemaphoreGive(mutex_);
                error = "secret_write_failed";
                return false;
            }
        }
        devices_[slot] = d;
        JsonDocument doc;
        auto array = doc.to<JsonArray>();
        for (const auto &item : devices_)
            if (item.id[0]) {
                auto row = array.add<JsonObject>();
                row["id"] = item.id;
                row["owner"] = item.owner;
                row["provider"] = item.provider;
                row["name"] = item.name;
                row["participant"] = item.participant;
                row["host"] = item.host;
                row["username"] = item.username;
                row["port"] = item.port;
                row["videoPort"] = item.videoPort;
                row["videoPath"] = item.videoPath;
                row["fps"] = item.fps;
                row["httpVideo"] = item.httpVideo;
                row["ringTone"] = item.ringTone;
                row["enabled"] = item.enabled;
            }
        String out;
        const auto written=serializeJson(doc, out);
        const bool ok = !doc.overflowed() && written==measureJson(doc) && out.length()==written && out.length() <= 4096 && config.putString("ic_devices", out);
        if (!ok) {
            devices_[slot] = before;
            if (password)
                config.putSecretString(secretKey, old);
            error = "config_write_failed";
        }
        const bool cancel =
            ok && !strcmp(state_.device, d.id) &&
            (state_.state == IntercomState::Ringing || state_.state == IntercomState::Connecting ||
             state_.state == IntercomState::Conversation);
        const uint32_t call = providerCall_;
        if (ok && (state_.state == IntercomState::Offline || state_.state == IntercomState::Ready ||
                   state_.state == IntercomState::Ended || state_.state == IntercomState::Error)) {
            state_.state = d.enabled ? IntercomState::Ready : IntercomState::Offline;
            strlcpy(state_.device, d.id, sizeof(state_.device));
            state_.capabilities = provider(d.provider)->capabilities(d);
            ++state_.revision;
            uiDirty_ = true;
        }
        xSemaphoreGive(mutex_);
        if (cancel)
            finished(d.id, call);
        diagnostic(ok ? "config_saved" : "config_write_failed");
        return ok;
    }
    bool IntercomService::device(const char *id, IntercomDevice &out) {
        if (!id || !mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        bool ok = false;
        for (const auto &d : devices_)
            if (!strcmp(d.id, id)) {
                out = d;
                ok = true;
                break;
            }
        xSemaphoreGive(mutex_);
        return ok;
    }
    size_t IntercomService::devices(const char *owner, IntercomDevice *out, size_t capacity) {
        if(!owner||!out||!mutex_||xSemaphoreTake(mutex_,0)!=pdTRUE)return 0;
        size_t count=0;
        for(const auto& d:devices_)if(d.id[0]&&!strcmp(d.owner,owner)&&count<capacity)out[count++]=d;
        xSemaphoreGive(mutex_);return count;
    }
    bool IntercomService::removeDevice(const char *owner,const char *id,String &error) {
        if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
        SecurityWriteGuard guard;
        if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){
            error="pin_locked";return false;
        }
        if(!owner||!id||!mutex_||xSemaphoreTake(mutex_,pdMS_TO_TICKS(20))!=pdTRUE){error="busy";return false;}
        int slot=-1;
        for(unsigned i=0;i<4;++i)if(!strcmp(devices_[i].id,id)&&!strcmp(devices_[i].owner,owner))slot=i;
        if(slot<0){xSemaphoreGive(mutex_);error="device_missing";return false;}
        const auto before=devices_[slot];devices_[slot]={};
        // Preserve all unknown/future profile fields by deleting only the matching row.
        JsonDocument doc;
        auto& config=ConfigService::instance();
        bool ok=!deserializeJson(doc,config.getString("ic_devices","[]"))&&doc.is<JsonArray>();
        if(ok){auto array=doc.as<JsonArray>();bool found=false;for(size_t i=0;i<array.size();++i)
            if(!strcmp(array[i]["id"]|"",id)&&!strcmp(array[i]["owner"]|"",owner)){array.remove(i);found=true;break;}
            String saved;const auto written=serializeJson(doc,saved);
            ok=found&&!doc.overflowed()&&written==measureJson(doc)&&saved.length()==written&&config.putString("ic_devices",saved);}
        if(!ok){devices_[slot]=before;error="config_write_failed";}
        const auto call=providerCall_;const bool cancel=ok&&!strcmp(state_.device,id);
        xSemaphoreGive(mutex_);
        if(ok){char key[12];snprintf(key,sizeof(key),"ic_pass%d",slot);config.putSecretString(key,"");
            VideoService::instance().close(owner);if(cancel)finished(id,call);}
        return ok;
    }
    bool IntercomService::startVideo(const char *owner,const char *id,String &error) {
        if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
        SecurityWriteGuard guard;
        if(SecurityService::instance().tabletLocked()){error="pin_locked";return false;}
        IntercomDevice d;int slot=-1;
        if(!owner||!id||!mutex_||xSemaphoreTake(mutex_,pdMS_TO_TICKS(20))!=pdTRUE){error="busy";return false;}
        for(unsigned i=0;i<4;++i)if(!strcmp(devices_[i].id,id)&&!strcmp(devices_[i].owner,owner)){d=devices_[i];slot=i;break;}
        auto* p=slot>=0?provider(d.provider):nullptr;
        const bool supported=p&&d.enabled&&p->capabilities(d).video;
        xSemaphoreGive(mutex_);
        AppRegistryEntry entry;
        const auto required=perm(AppPermission::Camera)|perm(AppPermission::Network)|perm(AppPermission::Intercom);
        if(!supported||!AppRegistry::instance().lookup(owner,entry)||entry.state!=AppRegistryState::Enabled||
            !ConfigService::instance().appEnabled(owner,true)||(entry.permissions&required)!=required){error="video_unavailable";return false;}
        VideoOptions options;strlcpy(options.host,d.host,sizeof(options.host));
        strlcpy(options.username,d.username,sizeof(options.username));options.port=d.videoPort;options.fps=d.fps;
        options.rtsp=!d.httpVideo&&strcmp(d.provider,"doorbird")!=0;
        const char* fallback=!strcmp(d.provider,"dahua")?"/cam/realmonitor?channel=1&subtype=1":
            !strcmp(d.provider,"hikvision")?"/Streaming/channels/102":"/bha-api/video.cgi";
        strlcpy(options.path,d.videoPath[0]?d.videoPath:fallback,sizeof(options.path));
        char key[12];snprintf(key,sizeof(key),"ic_pass%d",slot);
        String secret=ConfigService::instance().getSecretString(key);
        if(secret.length()>=sizeof(options.nativePassword)){error="video_password_length";return false;}
        strlcpy(options.nativePassword,secret.c_str(),sizeof(options.nativePassword));
        const bool ok=VideoService::instance().open(owner,options,error);
        memset(options.nativePassword,0,sizeof(options.nativePassword));return ok;
    }
    bool IntercomService::snapshot(IntercomSnapshot &out) const {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        out = state_;
        xSemaphoreGive(mutex_);
        return true;
    }
    bool IntercomService::incoming(const char *id, uint32_t call) {
        IntercomDevice owned;
        AppRegistryEntry app;
        if (!device(id, owned) || !AppRegistry::instance().lookup(owned.owner, app) ||
            app.state != AppRegistryState::Enabled ||
            !ConfigService::instance().appEnabled(owned.owner, true))
            return false;
        const auto required = perm(AppPermission::Network) | perm(AppPermission::Intercom);
        if ((app.permissions & required) != required)
            return false;
        if (!call || !id || !mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        const IntercomDevice *device = nullptr;
        for (const auto &d : devices_)
            if (d.enabled && !strcmp(d.id, id))
                device = &d;
        auto *p = device ? provider(device->provider) : nullptr;
        if (!p || !p->capabilities(*device).ringEvents) {
            xSemaphoreGive(mutex_);
            return false;
        }
        if (!strcmp(state_.device, id) && providerCall_ == call) {
            xSemaphoreGive(mutex_);
            return true;
        }
        if ((state_.state == IntercomState::Ringing || state_.state == IntercomState::Connecting ||
             state_.state == IntercomState::Conversation)) {
            const bool duplicate = !strcmp(state_.device, id) && providerCall_ == call;
            xSemaphoreGive(mutex_);
            return duplicate;
        }
        state_.state = IntercomState::Ringing;
        state_.call = ++nextCall_;
        if (!state_.call)
            state_.call = ++nextCall_;
        providerCall_ = call;
        state_.started = millis();
        strlcpy(state_.device, id, sizeof(state_.device));
        strlcpy(state_.name, device->name, sizeof(state_.name));
        strlcpy(state_.participant, device->participant, sizeof(state_.participant));
        state_.error[0] = 0;
        state_.muted = false;
        state_.capabilities = p->capabilities(*device);
        ++state_.revision;
        xSemaphoreGive(mutex_);
        uiDirty_ = true;
        diagnostic("ring");
        EventBus::instance().publish({EventType::DoorbellRing, nullptr});
        return true;
    }
    bool IntercomService::finished(const char *id, uint32_t call) {
        if (!id || !mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        const bool match = !strcmp(state_.device, id) && providerCall_ == call;
        const bool changed = match && state_.state != IntercomState::Ended;
        if (changed) {
            state_.state = IntercomState::Ended;
            ++state_.revision;
            uiDirty_ = true;
        }
        xSemaphoreGive(mutex_);
        if (changed) {stopPending_=true;queueAudioStop();}
        return match;
    }
    bool IntercomService::request(const char *owner, IntercomAction a, uint32_t call, bool value,
                                  bool confirmed) {
        const bool locked = SecurityService::instance().tabletLocked();
        IntercomSnapshot current;
        IntercomDevice owned;
        AppRegistryEntry entry;
        if (!snapshot(current) || !device(current.device, owned) ||
            !AppRegistry::instance().lookup(owned.owner, entry) ||
            entry.state != AppRegistryState::Enabled)
            return false;
        const auto permissions = entry.permissions;
        if (!(permissions & perm(AppPermission::Intercom)) ||
            !(permissions & perm(AppPermission::Network)))
            return false;
        if (a == IntercomAction::Door && !(permissions & perm(AppPermission::DoorOpen)))
            return false;
        if ((a == IntercomAction::Answer || a == IntercomAction::Mute) &&
            (!(permissions & perm(AppPermission::Audio)) ||
             !(permissions & perm(AppPermission::Microphone))))
            return false;
        if (!call || !mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return false;
        const IntercomDevice *d = nullptr;
        for (const auto &item : devices_)
            if (!strcmp(item.id, state_.device))
                d = &item;
        const bool sensitive = a != IntercomAction::Hangup;
        bool ok = d && (!owner || !strcmp(d->owner, owner)) && call == state_.call && !pending_ &&
                  (!sensitive || !locked);
        if (a == IntercomAction::Answer)
            ok &= state_.state == IntercomState::Ringing && state_.capabilities.answer;
        if (a == IntercomAction::Door)
            ok &= confirmed && state_.state == IntercomState::Conversation &&
                  state_.capabilities.door;
        if (a == IntercomAction::Mute)
            ok &= state_.state == IntercomState::Conversation && state_.capabilities.duplex;
        if (a == IntercomAction::Hangup)
            ok &= state_.state == IntercomState::Ringing ||
                  state_.state == IntercomState::Connecting ||
                  state_.state == IntercomState::Conversation;
        if (!ok) {
            xSemaphoreGive(mutex_);
            return false;
        }
        pending_ = true;
        pendingAction_ = a;
        pendingCall_ = call;
        pendingValue_ = value;
        strlcpy(pendingOwner_, owner ? owner : "", sizeof(pendingOwner_));
        xSemaphoreGive(mutex_);
        if (NetworkWorker::instance().submitPriority(commandJob, this, "intercom-cmd"))
            return true;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_ = false;
        xSemaphoreGive(mutex_);
        diagnostic("queue_full");
        return false;
    }
    void IntercomService::commandJob(void *ctx) {
        static_cast<IntercomService *>(ctx)->executeCommand();
    }
    void IntercomService::executeCommand() {
        SecurityWriteGuard security;
        IntercomDevice d;
        IntercomCapabilities caps;
        IntercomAction a;
        uint32_t call;
        bool value;
        IntercomProvider *p = nullptr;
        int slot = -1;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        a = pendingAction_;
        call = pendingCall_;
        value = pendingValue_;
        const bool stale =
            !pending_ || state_.call != call || state_.state == IntercomState::Ended ||
            (a != IntercomAction::Hangup && SecurityService::instance().tabletLocked());
        if (stale) {
            pending_ = false;
            xSemaphoreGive(mutex_);
            return;
        }
        for (unsigned i = 0; i < 4; ++i)
            if (!strcmp(devices_[i].id, state_.device)) {
                d = devices_[i];
                slot = i;
                break;
            }
        p = provider(d.provider);
        caps = state_.capabilities;
        if (slot < 0 || (a != IntercomAction::Hangup && !d.enabled)) {
            pending_ = false;
            xSemaphoreGive(mutex_);
            return;
        }
        if (a == IntercomAction::Answer) {
            state_.state = IntercomState::Connecting;
            ++state_.revision;
            uiDirty_ = true;
        }
        xSemaphoreGive(mutex_);
        AppRegistryEntry entry;
        const bool allowed = AppRegistry::instance().lookup(d.owner, entry) &&
                             entry.state == AppRegistryState::Enabled &&
                             ConfigService::instance().appEnabled(d.owner, true);
        const AppPermissions required =
            perm(AppPermission::Network) | perm(AppPermission::Intercom) |
            (a == IntercomAction::Door ? perm(AppPermission::DoorOpen) : 0) |
            ((a == IntercomAction::Answer || a == IntercomAction::Mute)
                 ? perm(AppPermission::Audio) | perm(AppPermission::Microphone)
                 : 0);
        if (a != IntercomAction::Hangup &&
            (!allowed || (entry.permissions & required) != required)) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            pending_ = false;
            if (state_.call == call) {
                state_.state = IntercomState::Ended;
                ++state_.revision;
                uiDirty_ = true;
            }
            xSemaphoreGive(mutex_);
            IntercomAudioService::instance().stop();
            return;
        }
        String error;
        bool ok = false;
        if (a == IntercomAction::Hangup && !caps.hangup)
            ok = true;
        else if (a == IntercomAction::Mute) {
            IntercomAudioService::instance().mute(value);
            ok = true;
        } else if (p && slot >= 0) {
            char key[12];
            snprintf(key, sizeof(key), "ic_pass%d", slot);
            const String secret = ConfigService::instance().getSecretString(key);
            ok = p->command(d, a, value, secret, error);
        }
        bool current;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        current = state_.call == call && state_.state != IntercomState::Ended;
        xSemaphoreGive(mutex_);
        if (a == IntercomAction::Answer && ok && caps.duplex && current)
            ok = IntercomAudioService::instance().start(true);
        if (a == IntercomAction::Hangup || !ok)
            IntercomAudioService::instance().stop();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_ = false;
        if (state_.call == call && state_.state != IntercomState::Ended) {
            if (ok) {
                if (a == IntercomAction::Answer)
                    state_.state = IntercomState::Conversation;
                else if (a == IntercomAction::Hangup)
                    state_.state = IntercomState::Ended;
                else if (a == IntercomAction::Mute)
                    state_.muted = value;
            } else {
                state_.state = IntercomState::Error;
                strlcpy(state_.error, "provider_command_failed", sizeof(state_.error));
            }
            ++state_.revision;
            uiDirty_ = true;
        }
        const bool staleCompletion = state_.call != call || state_.state == IntercomState::Ended;
        xSemaphoreGive(mutex_);
        if (a == IntercomAction::Answer && staleCompletion)
            IntercomAudioService::instance().stop();
        diagnostic(ok ? "command_ok" : "command_failed");
    }
    void IntercomService::detachOwner(const char *owner) {
        IntercomSnapshot s;
        IntercomDevice d;
        if (!owner || !snapshot(s) || !device(s.device, d) || strcmp(owner, d.owner))
            return;
        // Closing a UI does not disable event reception. Disable/uninstall invalidates
        // a ringing call; a closed foreground conversation is always torn down.
        AppRegistryEntry entry;
        const bool enabled = AppRegistry::instance().lookup(owner, entry) &&
                             entry.state == AppRegistryState::Enabled &&
                             ConfigService::instance().appEnabled(owner, true);
        if (s.state == IntercomState::Ringing && enabled)
            return;
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return;
        if (state_.call == s.call) {
            state_.state = IntercomState::Ended;
            ++state_.revision;
            uiDirty_ = true;
        }
        xSemaphoreGive(mutex_);
        stopPending_=true;queueAudioStop();
    }
    void IntercomService::queueAudioStop(){
        if(!stopPending_.exchange(false))return;
        if(!NetworkWorker::instance().submitPriority([](void*){IntercomAudioService::instance().stop();},nullptr,"intercom-stop"))stopPending_=true;
    }
    void IntercomService::diagnostic(const char *code) {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return;
        auto &log = logs_[logNext_++ % 32];
        log.at = millis();
        strlcpy(log.code, code ? code : "unknown", sizeof(log.code));
        logCount_ = std::min(32u, logCount_ + 1);
        xSemaphoreGive(mutex_);
    }
    String IntercomService::diagnostics() const {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
            return "{\"error\":\"busy\"}";
        JsonDocument doc;
        doc["state"] = intercomStateName(state_.state);
        doc["revision"] = state_.revision;
        doc["audio_active"] = IntercomAudioService::instance().active();
        doc["internal_free"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        doc["internal_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        doc["psram_free"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        VideoSnapshot video;
        if (VideoService::instance().diagnosticSnapshot(video)) {
            auto item = doc["video"].to<JsonObject>();
            item["active"] = video.active;
            item["connecting"] = video.connecting;
            item["frames"] = video.frames;
            item["codec"] = video.codec;item["width"]=video.width;item["height"]=video.height;
            item["published_fps"] = video.fpsMilli/1000.0f;
            item["last_frame_ms"] = video.lastFrameAt;
            item["frame_budget_bytes"] = VideoService::frameBudget;
            item["encoded_limit_bytes"] = VideoService::encodedLimit;
            item["dropped"] = video.dropped;
            item["errors"] = video.errors;
            item["error"] = video.error;
        }
        auto list = doc["events"].to<JsonArray>();
        for (unsigned i = 0; i < logCount_; ++i) {
            const auto &log = logs_[(logNext_ - logCount_ + i) % 32];
            auto e = list.add<JsonObject>();
            e["at_ms"] = log.at;
            e["code"] = log.code;
        }
        String out;
        serializeJson(doc, out);
        xSemaphoreGive(mutex_);
        return out;
    }
    void IntercomService::loopUi() {
        queueAudioStop();
        const bool notified = uiDirty_.exchange(false, std::memory_order_acq_rel);
        IntercomSnapshot s;
        if (!snapshot(s)) {
            if (notified)
                uiDirty_ = true;
            return;
        }
        if(s.state==IntercomState::Ringing||s.state==IntercomState::Connecting||s.state==IntercomState::Conversation){
            IntercomDevice owned;AppRegistryEntry entry;
            if(device(s.device,owned)&&(!AppRegistry::instance().lookup(owned.owner,entry)||
                entry.state!=AppRegistryState::Enabled||!ConfigService::instance().appEnabled(owned.owner,true))){
                detachOwner(owned.owner);return;
            }
        }
        if (s.state == IntercomState::Ringing && uint32_t(millis()-s.started)>=60000) {
            request(nullptr, IntercomAction::Hangup, s.call, false, false);
            return;
        }
        const bool visible = s.state == IntercomState::Ringing ||
                             s.state == IntercomState::Connecting ||
                             s.state == IntercomState::Conversation;
        const bool changed = shownRevision_ != s.revision;
        shownRevision_ = s.revision;
        if (!visible) {
            if (overlay.root) {
                lv_obj_delete(overlay.root);
                overlay = {};
            }
            return;
        }
        if (changed)
            IdleService::instance().activity();
        if (!overlay.root) {
            overlay.root = lv_obj_create(lv_layer_sys());
            lv_obj_set_size(overlay.root, 520, 260);
            lv_obj_center(overlay.root);
            lv_obj_set_style_bg_opa(overlay.root, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(overlay.root, lv_color_hex(0x182532), 0);
            lv_obj_set_flex_flow(overlay.root, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(overlay.root, 20, 0);
            overlay.label = lv_label_create(overlay.root);
            auto *row = lv_obj_create(overlay.root);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_style_pad_column(row, 6, 0);
            lv_obj_set_size(row, LV_PCT(100), 90);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            overlay.answer = button(row, "Annehmen", IntercomAction::Answer);
            button(row, "Schliessen", IntercomAction::Hangup);
            overlay.mute = button(row, "Stumm", IntercomAction::Mute);
            overlay.door = button(row, "Tuer oeffnen", IntercomAction::Door);
        }
        if (lv_obj_get_child(lv_layer_sys(), -1) != overlay.root)
            lv_obj_move_foreground(overlay.root);
        if (overlay.call != s.call || SecurityService::instance().tabletLocked() ||
            (overlay.doorConfirmCall && int32_t(millis() - overlay.doorUntil) >= 0)) {
            overlay.doorConfirmCall = 0;
            lv_label_set_text(lv_obj_get_child(overlay.door, 0), "Tuer oeffnen");
        }
        overlay.call = s.call;
        if (changed) {
            String text = String(s.name) + "\n" + s.participant + "\n" + intercomStateName(s.state);
            lv_label_set_text(overlay.label, text.c_str());
        }
        auto disabled = [](lv_obj_t *o, bool v) {
            if (bool(lv_obj_has_state(o, LV_STATE_DISABLED)) != v) {
                if (v)
                    lv_obj_add_state(o, LV_STATE_DISABLED);
                else
                    lv_obj_remove_state(o, LV_STATE_DISABLED);
            }
        };
        const bool locked = SecurityService::instance().tabletLocked();
        disabled(overlay.answer,
                 locked || !s.capabilities.answer || s.state != IntercomState::Ringing);
        disabled(overlay.door,
                 locked || !s.capabilities.door || s.state != IntercomState::Conversation);
        disabled(overlay.mute,
                 locked || !s.capabilities.duplex || s.state != IntercomState::Conversation);
        if (changed)
            lv_label_set_text(lv_obj_get_child(overlay.mute, 0), s.muted ? "Ton an" : "Stumm");
    }
} // namespace hp
