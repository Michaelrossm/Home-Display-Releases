#pragma once
#include "FileBrowserService.h"
#include "ImageResourceService.h"
#include "ImageRenderPolicy.h"
#include "../core/LatencyHistogram.h"
#include <array>
namespace hp {
// UI-owned, bounded cache. Pixels/descriptors are shared with displayed widgets;
// evicting a cache entry can NEVER invalidate a displayed image.
class PhotoImageCache {
public:
    static constexpr size_t budget=6*1024*1024;
    using Lease=ImageResourceService::Lease;
    ~PhotoImageCache(){clear();}
    void select(const String& uri,uint16_t edge);
    void seed(const String& uri,std::shared_ptr<Lease> image,bool sourceComplete,uint16_t edge);
    void poll(const String& next,bool allowQuality,bool allowPrefetch);
    std::shared_ptr<Lease> visible()const;
    bool final()const;
    bool busy()const{return bool(pending_);}
    bool failed()const{return failed_;}
    DirectoryPageCache::Revision revision()const{return revision_;}
    const String& message()const{return message_;}
    void clear();
    void invalidate(const String& uri);
    static String metricsJson();
private:
    struct Entry {String uri;uint16_t edge=0;uint32_t used=0;std::shared_ptr<Lease> lease;};
    std::array<Entry,3> entries_{};
    std::shared_ptr<ImageRequest> pending_;std::shared_ptr<ImageSource> source_;
    String target_,pendingUri_,sourceUri_,failedNext_,message_;uint16_t edge_=1280,pendingEdge_=0;
    uint32_t selectedAt_=0;bool failed_=false,first_=false,finished_=false,prefetch_=false;
    DirectoryPageCache::Revision revision_{};
    bool start(const String&,uint16_t,bool prefetch);
    bool retain(const String&,uint16_t,std::shared_ptr<DecodedImage>);
    size_t bytes()const;
    static bool progressive(const String&);
};
}
