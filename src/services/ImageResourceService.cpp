#include "ImageResourceService.h"
#include <algorithm>
namespace hp {
ImageResourceService& ImageResourceService::instance(){static ImageResourceService service;return service;}
ImageResourceService::Lease::~Lease(){resource.reset();if(id)ImageResourceService::instance().release(owner,id);}
std::shared_ptr<ImageResourceService::Lease> ImageResourceService::lease(const String& owner,std::shared_ptr<DecodedImage> image){
    const uint32_t id=store(owner,std::move(image));if(!id)return {};auto resource=acquire(owner,id);if(!resource){release(owner,id);return {};}
    auto result=std::make_shared<Lease>();result->owner=owner;result->id=id;result->resource=std::move(resource);return result;
}
bool ImageResourceService::begin(){if(!mutex_)mutex_=xSemaphoreCreateMutex();return mutex_!=nullptr;}
ImageResourceService::Resource::Resource(std::shared_ptr<DecodedImage> image):pixels(std::move(image)){
    descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf=pixels->rgb565?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_ARGB8888;
    descriptor.header.w=pixels->width;descriptor.header.h=pixels->height;
    descriptor.header.stride=pixels->width*(pixels->rgb565?2:4);
    descriptor.data_size=descriptor.header.stride*pixels->height;descriptor.data=pixels->pixels;
}
uint32_t ImageResourceService::store(const String& owner,std::shared_ptr<DecodedImage> image){
    if(!mutex_||!image||!image->pixels||!owner.length())return 0;
    const size_t requested=size_t(image->width)*image->height*(image->rgb565?2:4);if(requested>kBudget)return 0;
    xSemaphoreTake(mutex_,portMAX_DELAY);
    size_t used=0;for(const auto& s:slots_)if(s.resource)used+=s.resource->descriptor.data_size;
    Slot* selected=nullptr;
    for(;;){for(auto& s:slots_)if(!s.resource){selected=&s;break;}
        if(selected&&used+requested<=kBudget)break;
        Slot* oldest=nullptr;
        for(auto& s:slots_)if(s.resource&&!s.active&&s.resource.use_count()==1&&(!oldest||int32_t(s.last-oldest->last)<0))oldest=&s;
        if(!oldest){xSemaphoreGive(mutex_);return 0;}
        used-=oldest->resource->descriptor.data_size;*oldest={};
    }
    sequence_=(sequence_+1)&0x1fffffff;if(!sequence_)++sequence_;
    selected->id=sequence_;selected->owner=owner;selected->last=millis();selected->active=true;
    selected->resource=std::make_shared<Resource>(std::move(image));
    const uint32_t id=selected->id;xSemaphoreGive(mutex_);return id;
}
std::shared_ptr<ImageResourceService::Resource> ImageResourceService::acquire(const String& owner,uint32_t handle){
    // One scheduler tick accommodates a brief concurrent metadata update without
    // waiting indefinitely on the LVGL caller.
    if(!mutex_||xSemaphoreTake(mutex_,1)!=pdTRUE)return {};
    std::shared_ptr<Resource> result;for(auto& s:slots_)if(s.id==handle&&s.active&&s.owner==owner){s.last=millis();result=s.resource;break;}
    xSemaphoreGive(mutex_);return result;
}
bool ImageResourceService::release(const String& owner,uint32_t handle){
    // A dropped lease must not leak an active handle just because a worker is
    // briefly updating metadata. No I/O or LVGL call is made under this mutex.
    if(!mutex_||xSemaphoreTake(mutex_,portMAX_DELAY)!=pdTRUE)return false;
    bool found=false;for(auto& s:slots_)if(s.id==handle&&s.owner==owner){s.active=false;s.id=0;if(s.resource.use_count()==1)s={};found=true;break;}
    xSemaphoreGive(mutex_);return found;
}
void ImageResourceService::releaseOwner(const String& owner){
    if(!mutex_)return;xSemaphoreTake(mutex_,portMAX_DELAY);
    for(auto& s:slots_)if(s.owner==owner){s.active=false;s.id=0;if(!s.resource||s.resource.use_count()==1)s={};}
    xSemaphoreGive(mutex_);
}
}
