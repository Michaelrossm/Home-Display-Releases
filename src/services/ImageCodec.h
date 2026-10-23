#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
namespace hp {
struct DecodedImage {
    uint8_t* pixels=nullptr;uint32_t width=0,height=0;bool rgb565=false;
    ~DecodedImage();
    DecodedImage()=default;DecodedImage(const DecodedImage&)=delete;DecodedImage& operator=(const DecodedImage&)=delete;
};
class ImageCodec {
public:
    static constexpr size_t maxFileBytes=4*1024*1024,maxSourcePixels=4*1024*1024;
    static bool supported(const char* path);
    static bool dimensions(const uint8_t* bytes,size_t size,uint32_t& width,uint32_t& height);
    // Worker-only. GIF shows first frame; never creates an animation timer.
    // Shared by all consumers. rgb565 opts into bounded opaque/black-composited
    // contain rendering (JPEG downsampling, PNG rows, WebP scaling); retain the
    // default for alpha-sensitive icons and cover/center crops. Limits/cache
    // ownership belong to callers, not separate app-specific decoders.
    static std::shared_ptr<DecodedImage> decode(uint8_t* bytes,size_t size,uint32_t maxWidth,uint32_t maxHeight,bool cover=false,const std::atomic<bool>* cancelled=nullptr,bool center=false,bool rgb565=false);
};
}
