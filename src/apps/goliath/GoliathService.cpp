#include "GoliathService.h"
#include "GoliathSettings.h"
#include "GoliathDiagnostics.h"
#include "GoliathEventMonitor.h"
#include "../../services/NetworkService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/IntercomService.h"
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <cstring>

namespace hp {
GoliathService& GoliathService::instance() { static GoliathService service; return service; }

bool GoliathService::begin() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!lock_ || !GoliathDiagnostics::instance().begin()) return false;
    settingsChanged();
    return BackgroundScheduler::instance().add(this) && GoliathEventMonitor::instance().begin();
}

namespace { constexpr const char* kGoliathVideoOwner = "goliath-native"; }

bool GoliathService::startCamera(String& error) {
    const auto settings = GoliathSettings::instance().load();
    if (!settings.enabled || !settings.host.length()) {
        error = "goliath_not_configured";
        return false;
    }
    VideoOptions opts;
    if (settings.host.length() >= sizeof(opts.host) ||
        settings.username.length() >= sizeof(opts.username) ||
        settings.password.length() >= sizeof(opts.nativePassword)) {
        error = "goliath_options";
        return false;
    }
    strlcpy(opts.host, settings.host.c_str(), sizeof(opts.host));
    strlcpy(opts.username, settings.username.c_str(), sizeof(opts.username));
    strlcpy(opts.nativePassword, settings.password.c_str(), sizeof(opts.nativePassword));
    strlcpy(opts.path, settings.useExtraStream
        ? "/cam/realmonitor?channel=1&subtype=1"
        : "/cam/realmonitor?channel=1&subtype=0", sizeof(opts.path));
    opts.port = settings.rtspPort;
    opts.width = settings.useExtraStream ? 352 : 640;
    opts.height = settings.useExtraStream ? 288 : 480;
    opts.fps = 5;
    const bool started = VideoService::instance().open(kGoliathVideoOwner, opts, error);
    memset(opts.nativePassword, 0, sizeof(opts.nativePassword));
    return started;
}

void GoliathService::stopCamera() {
    VideoService::instance().close(kGoliathVideoOwner);
}

bool GoliathService::cameraSnapshot(VideoSnapshot& result) const {
    return VideoService::instance().snapshot(kGoliathVideoOwner, result);
}

std::shared_ptr<VideoFrame> GoliathService::cameraFrame() const {
    return VideoService::instance().frame(kGoliathVideoOwner);
}

void GoliathService::settingsChanged() {
    const auto settings = GoliathSettings::instance().load();
    if (lock_ && xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
        enabled_ = settings.enabled;
        diagnosticsEnabled_ = settings.diagnosticsEnabled;
        xSemaphoreGive(lock_);
    }
    if (!settings.enabled) stopCamera();
    GoliathEventMonitor::instance().settingsChanged();
    if (!settings.enabled) setState(GoliathConnectionState::Disabled, "Integration ist ausgeschaltet.", "DISABLED");
    else if (!settings.host.length()) setState(GoliathConnectionState::NotConfigured, "Adresse der Aussenstation fehlt.", "NOT_CONFIGURED");
    else setState(GoliathConnectionState::Offline, "Verbindung noch nicht geprueft.", "CONFIGURED");
}

bool GoliathService::requestProbe() {
    const auto settings = GoliathSettings::instance().load();
    if (!settings.enabled || !settings.host.length() || !NetworkService::instance().online()) return false;
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    if (probeQueued_) { xSemaphoreGive(lock_); return false; }
    probeQueued_ = true;
    state_ = GoliathConnectionState::Queued;
    status_ = "Verbindungspruefung eingeplant.";
    xSemaphoreGive(lock_);
    if (NetworkWorker::instance().submit(probeJob, this, "goliath-http")) return true;
    setState(GoliathConnectionState::Error, "Netzwerk-Worker ist ausgelastet.", "QUEUE_FULL");
    return false;
}

void GoliathService::probeJob(void* context) { static_cast<GoliathService*>(context)->executeProbe(); }

void GoliathService::executeProbe() {
    const auto settings = GoliathSettings::instance().load();
    setState(GoliathConnectionState::Connecting, "Pruefe HTTP-Verbindung ...", "HTTP_START");
    NetworkClient client;
    HTTPClient http;
    const String url = "http://" + settings.host + ":" + String(settings.httpPort) + "/";
    http.setConnectTimeout(2500);
    http.setTimeout(3500);
    if (!http.begin(client, url)) {
        setState(GoliathConnectionState::Error, "HTTP-Verbindung konnte nicht vorbereitet werden.", "HTTP_BEGIN");
        return;
    }
    if (settings.username.length()) http.setAuthorization(settings.username.c_str(), settings.password.c_str());
    const int code = http.GET();
    http.end();
    if (code >= 200 && code < 400) setState(GoliathConnectionState::Reachable, "Aussenstation per HTTP erreichbar.", "HTTP_OK");
    else if (code == 401 || code == 403) setState(GoliathConnectionState::AuthenticationRequired, "Aussenstation erreichbar; Zugangsdaten pruefen.", "HTTP_AUTH");
    else if (code > 0) setState(GoliathConnectionState::Error, "Aussenstation antwortet mit HTTP " + String(code) + ".", "HTTP_STATUS");
    else setState(GoliathConnectionState::Offline, "Aussenstation nicht erreichbar.", "HTTP_OFFLINE");
}

void GoliathService::setState(GoliathConnectionState state, const String& status, const char* diagnosticCode) {
    bool diagnostics = false;
    if (lock_ && xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
        state_ = state;
        status_ = status;
        probeQueued_ = false;
        lastProbeAt_ = millis();
        diagnostics = diagnosticsEnabled_;
        xSemaphoreGive(lock_);
    }
    // Never include host names, credentials or complete URLs in the ring log.
    if (diagnostics) {GoliathDiagnostics::instance().add(diagnosticCode, status);IntercomService::instance().diagnostic(diagnosticCode);}
}

void GoliathService::backgroundTick() {
    bool enabled = false;
    uint32_t last = 0;
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) == pdTRUE) { enabled = enabled_; last = lastProbeAt_; xSemaphoreGive(lock_); }
    if (!enabled || !NetworkService::instance().online()) return;
    if (millis() - last >= 60000UL) requestProbe();
}

GoliathConnectionState GoliathService::state() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return GoliathConnectionState::Error;
    const auto state = state_; xSemaphoreGive(lock_); return state;
}

String GoliathService::eventStatus() const { return GoliathEventMonitor::instance().stateName(); }
uint32_t GoliathService::observedCallEvents() const { return GoliathEventMonitor::instance().observedCalls(); }

String GoliathService::status() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return "Status nicht verfuegbar.";
    const String status = status_; xSemaphoreGive(lock_); return status;
}
}
