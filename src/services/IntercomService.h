#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include "intercom/IntercomProvider.h"
namespace hp {
    enum class IntercomState : uint8_t {
        Offline,
        Ready,
        Ringing,
        Connecting,
        Conversation,
        Ended,
        Error
    };
    struct IntercomSnapshot {
        IntercomState state = IntercomState::Offline;
        uint32_t revision = 0, call = 0, started = 0;
        char device[33]{}, name[65]{}, participant[65]{}, error[65]{};
        bool muted = false;
        IntercomCapabilities capabilities;
    };
    class IntercomService {
      public:
        static IntercomService &instance();
        bool begin();
        bool registerProvider(IntercomProvider *);
        bool configure(const IntercomDevice &, const String *password, String &error);
        bool device(const char *id, IntercomDevice &out);
        size_t devices(const char *owner, IntercomDevice *out, size_t capacity);
        bool removeDevice(const char *owner, const char *id, String &error);
        bool startVideo(const char *owner, const char *id, String &error);
        bool snapshot(IntercomSnapshot &) const;
        // Trusted adapters ONLY: call IDs make duplicates/stale events harmless.
        bool incoming(const char *device, uint32_t providerCall);
        bool finished(const char *device, uint32_t providerCall);
        bool request(const char *owner, IntercomAction, uint32_t call, bool value, bool confirmed);
        void detachOwner(const char *owner);
        void loopUi();
        bool urgent() const { return uiDirty_.load(std::memory_order_acquire); }
        String diagnostics() const;
        void diagnostic(const char *code);

      private:
        static void commandJob(void *);
        void executeCommand();
        void queueAudioStop();
        IntercomProvider *provider(const char *id) const;
        mutable SemaphoreHandle_t mutex_ = nullptr;
        IntercomDevice devices_[4]{};
        IntercomProvider *providers_[4]{};
        IntercomSnapshot state_{};
        uint32_t nextCall_ = 0, providerCall_ = 0;
        bool pending_ = false;
        IntercomAction pendingAction_ = IntercomAction::Hangup;
        uint32_t pendingCall_ = 0;
        bool pendingValue_ = false;
        char pendingOwner_[65]{};
        struct Log {
            uint32_t at = 0;
            char code[33]{};
        };
        Log logs_[32]{};
        unsigned logNext_ = 0, logCount_ = 0;
        std::atomic<bool> uiDirty_{false};
        std::atomic<bool> stopPending_{false};
        uint32_t shownRevision_ = 0;
    };
    const char *intercomStateName(IntercomState);
} // namespace hp
