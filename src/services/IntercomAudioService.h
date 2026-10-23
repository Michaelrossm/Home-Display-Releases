#pragma once
#include <Arduino.h>
#include <atomic>
namespace hp {
    // One firmware audio source, existing BSP hardware and NetworkWorker only.
    class IntercomAudioService {
      public:
        static IntercomAudioService &instance();
        bool begin();
        bool start(bool microphone);
        void stop();
        bool receiveG711(const uint8_t *, size_t, bool alaw);
        size_t captureG711(uint8_t *, size_t, bool alaw);
        void mute(bool value) { muted_.store(value, std::memory_order_release); }
        bool active() const { return active_.load(std::memory_order_acquire); }

      private:
        static void maintenance(void*);
        void pollRing();
        bool startAudio(bool microphone,bool ringing);
        bool maintenanceRegistered_=false,ringing_=false,microphoneActive_=false;
        uint32_t ringCall_=0,ringNext_=0;
        uint32_t source_ = 0, previous_ = 0;
        bool resume_ = false;
        std::atomic<bool> active_{false}, muted_{false};
        int16_t pcm_[640]{};
    };
} // namespace hp
