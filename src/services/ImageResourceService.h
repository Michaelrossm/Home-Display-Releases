#pragma once
#include "ImageCodec.h"
#include <Arduino.h>
#include <lvgl.h>
#include <array>
#include <freertos/semphr.h>

namespace hp {
// One bounded resource pool. Widgets pin descriptors; workers only create pixels.
class ImageResourceService {
public:
    static constexpr size_t kBudget=8*1024*1024,kSlots=32;
    struct Resource {
        std::shared_ptr<DecodedImage> pixels;
        lv_image_dsc_t descriptor{};
        explicit Resource(std::shared_ptr<DecodedImage> image);
    };
    static ImageResourceService& instance();
    struct Lease {std::shared_ptr<Resource> resource;String owner;uint32_t id=0;~Lease();};
    std::shared_ptr<Lease> lease(const String& owner,std::shared_ptr<DecodedImage> image);
    bool begin(); // UI startup, before any worker job.
    uint32_t store(const String& owner,std::shared_ptr<DecodedImage> image);
    std::shared_ptr<Resource> acquire(const String& owner,uint32_t handle);
    bool release(const String& owner,uint32_t handle);
    void releaseOwner(const String& owner);
private:
    struct Slot {String owner;uint32_t id=0,last=0;bool active=false;std::shared_ptr<Resource> resource;};
    std::array<Slot,kSlots> slots_{};SemaphoreHandle_t mutex_=nullptr;uint32_t sequence_=0;
};
}
