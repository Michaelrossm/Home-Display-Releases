#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../core/BackgroundScheduler.h"
#include "../../services/VideoService.h"

namespace hp {

enum class GoliathConnectionState : uint8_t { Disabled, NotConfigured, Offline, Queued, Connecting, Reachable, AuthenticationRequired, Error };

class GoliathService : public IBackgroundJob {
public:
    static GoliathService& instance();
    bool begin();
    bool requestProbe();
    // Explicit foreground camera preview; never activated by background probes.
    bool startCamera(String& error);
    void stopCamera();
    bool cameraSnapshot(VideoSnapshot& result) const;
    std::shared_ptr<VideoFrame> cameraFrame() const;
    void settingsChanged();
    void backgroundTick() override;
    GoliathConnectionState state() const;
    String status() const;
    String eventStatus() const;
    uint32_t observedCallEvents() const;
private:
    static void probeJob(void* context);
    void executeProbe();
    void setState(GoliathConnectionState state, const String& status, const char* diagnosticCode);

    SemaphoreHandle_t lock_ = nullptr;
    GoliathConnectionState state_ = GoliathConnectionState::NotConfigured;
    String status_;
    bool enabled_ = false;
    bool diagnosticsEnabled_ = false;
    bool probeQueued_ = false;
    uint32_t lastProbeAt_ = 0;
};

}
