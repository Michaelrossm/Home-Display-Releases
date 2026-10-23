#pragma once
#include <Arduino.h>
#include <atomic>
#include <memory>
#include <mutex>
#include "ImageCodec.h"
namespace hp {
    struct VideoOptions {
        char host[97]{}, path[193]{}, username[65]{}, secretKey[49]{};
        // Only trusted native callers set this; Lua cannot inject it. Never log or persist.
        char nativePassword[193]{};
        uint16_t port = 554, width = 640, height = 480;
        uint8_t fps = 5;
        bool rtsp = true;
    };
    struct VideoSnapshot {
        uint32_t generation = 0, revision = 0, frames = 0, dropped = 0, errors = 0;
        uint32_t fpsMilli = 0, lastFrameAt = 0;
        uint16_t width = 0, height = 0;
        char codec[8]{};
        bool active = false, connecting = false;
        char error[49]{};
    };
    struct VideoFrame {
        std::shared_ptr<DecodedImage> image;
        uint32_t revision = 0, generation = 0, at = 0;
    };
    class VideoService {
      public:
        static constexpr size_t encodedLimit = 192 * 1024, frameBudget = 3 * 640 * 480 * 2;
        static VideoService &instance();
        bool begin();
        bool open(const char *owner, const VideoOptions &, String &error);
        void close(const char *owner);
        bool snapshot(const char *owner, VideoSnapshot &) const;
        bool diagnosticSnapshot(VideoSnapshot &) const;
        std::shared_ptr<VideoFrame> frame(const char *owner) const;
        bool uiPending() const { return uiPending_.load(std::memory_order_acquire); }
        void uiConsumed() { uiPending_.store(false, std::memory_order_release); }
        bool submit(const uint8_t *bytes, size_t count, bool h264, uint32_t stamp = 0);

      private:
        struct Runtime;
        Runtime *runtime_ = nullptr;
        static void maintenance(void *);
        static void decodeJob(void *);
        void poll();
        void decode(void *);
        void fail(const char *, uint32_t generation = 0);
        mutable std::mutex mutex_;
        VideoSnapshot state_;
        VideoOptions options_;
        char owner_[65]{};
        std::shared_ptr<VideoFrame> latest_;
        std::weak_ptr<DecodedImage> images_[3];
        std::atomic<bool> decoding_{false};
        bool registered_ = false;
        uint32_t retryAt_ = 0;
        uint8_t retries_ = 0;
        uint32_t fpsAt_=0,fpsFrames_=0;
        void *decoderState_ = nullptr;
        std::atomic<bool> decoderAlive_{false};
        std::atomic<bool> resync_{false}, uiPending_{false};
    };
} // namespace hp
