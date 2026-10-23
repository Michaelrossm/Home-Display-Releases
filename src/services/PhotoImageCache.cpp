#include "PhotoImageCache.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <algorithm>
namespace hp {namespace {
struct Metrics {LatencyHistogram queue,read,decode,first,final;std::atomic<uint32_t> hits{0},cancelled{0},failures{0},peak{0};} metrics;
}
bool PhotoImageCache::progressive(const String& uri){String name=uri;name.toLowerCase();return name.endsWith(".jpg")||name.endsWith(".jpeg")||name.endsWith(".webp");}
size_t PhotoImageCache::bytes()const{size_t n=0;for(const auto& e:entries_)if(e.lease)n+=e.lease->resource->descriptor.data_size;return n;}
void PhotoImageCache::clear(){if(pending_){pending_->canceled=true;++metrics.cancelled;}pending_.reset();source_.reset();sourceUri_=pendingUri_=failedNext_=message_="";for(auto& e:entries_)e={};target_="";failed_=first_=finished_=prefetch_=false;}
void PhotoImageCache::invalidate(const String& uri){for(auto& e:entries_)if(e.uri==uri)e={};if(pendingUri_==uri&&pending_){pending_->canceled=true;pending_.reset();}if(sourceUri_==uri){source_.reset();sourceUri_="";}if(target_==uri){target_="";failed_=false;}}
void PhotoImageCache::select(const String& uri,uint16_t edge){
    edge=ImageRenderPolicy::quality(edge)?edge:1280;
    if(target_==uri&&edge_==edge)return;
    if(pending_&&(pendingUri_!=uri||pendingEdge_>edge)){pending_->canceled=true;pending_.reset();++metrics.cancelled;}
    if(edge<edge_)for(auto& e:entries_)if(e.lease&&e.edge>edge){const auto& image=*e.lease->resource->pixels;if(std::max(image.width,image.height)<=edge)e.edge=edge;else e={};}
    if(pending_&&pendingUri_==uri)prefetch_=false;
    if(sourceUri_!=uri){source_.reset();sourceUri_="";}
    target_=uri;edge_=ImageRenderPolicy::quality(edge)?edge:1280;selectedAt_=millis();failed_=first_=finished_=false;message_="";
    if(visible())++metrics.hits;
}
std::shared_ptr<PhotoImageCache::Lease> PhotoImageCache::visible()const{
    const Entry* best=nullptr;for(const auto& e:entries_)if(e.lease&&e.uri==target_&&(!best||e.edge>best->edge))best=&e;return best?best->lease:nullptr;
}
bool PhotoImageCache::final()const{for(const auto& e:entries_)if(e.lease&&e.uri==target_&&e.edge>=edge_)return true;return false;}
void PhotoImageCache::seed(const String& uri,std::shared_ptr<Lease> image,bool complete,uint16_t edge){
    if(!image)return;revision_=FileBrowserService::directoryRevision();select(uri,edge);entries_[0]={uri,uint16_t(complete?edge:1),millis(),std::move(image)};
}
bool PhotoImageCache::start(const String& uri,uint16_t edge,bool prefetch){
    if(pending_||!uri.length())return false;
    if(!prefetch){String lower=uri;lower.toLowerCase();const auto bounds=ImageRenderPolicy::fit(edge,edge,edge,edge);const size_t pixels=size_t(bounds.width)*bounds.height;
        const size_t reserve=pixels*(lower.endsWith(".webp")?6:lower.endsWith(".png")?2:4)+4*1024*1024;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<reserve)for(auto& entry:entries_)if(entry.uri!=target_)entry={};}
    pendingUri_=uri;pendingEdge_=edge;prefetch_=prefetch;
    pending_=FileBrowserService::image(uri,edge,edge,false,true,sourceUri_==uri?source_:std::shared_ptr<ImageSource>{});return true;
}
bool PhotoImageCache::retain(const String& uri,uint16_t edge,std::shared_ptr<DecodedImage> image){
    const size_t needed=size_t(image->width)*image->height*(image->rgb565?2:4);if(needed>budget)return false;
    // Check that the local cache can make room without dropping a newer visible
    // version. This check does not mutate entries if the upgrade cannot fit.
    size_t reclaimable=0;unsigned reclaimableSlots=0,emptySlots=0;
    for(const auto& e:entries_){
        if(!e.lease){++emptySlots;continue;}
        if(e.uri!=target_||(e.uri==uri&&e.edge<=edge)){
            reclaimable+=e.lease->resource->descriptor.data_size;++reclaimableSlots;
        }
    }
    if((bytes()-reclaimable)+needed>budget||(!emptySlots&&!reclaimableSlots))return false;
    // Reserve the new resource before replacing any displayed preview. A failed
    // pool allocation must leave the currently visible image untouched.
    auto pin=ImageResourceService::instance().lease("native.photos",std::move(image));
    if(!pin)return false;
    // The pool now owns the new pixels; only now is it safe to evict cached ones.
    // Prefer stale, non-visible entries, then older versions of this same URI.
    for(auto& e:entries_)if(e.uri==uri&&e.edge<=edge)e={};
    auto empty=[&]()->Entry*{for(auto& e:entries_)if(!e.lease)return &e;return nullptr;};
    while(bytes()+needed>budget||!empty()){
        Entry* oldest=nullptr;
        for(auto& e:entries_)if(e.lease&&e.uri!=target_&&(!oldest||int32_t(e.used-oldest->used)<0))oldest=&e;
        if(!oldest)return false; // New pin is released; remaining entries are still valid.
        *oldest={};
    }
    *empty()={uri,edge,millis(),std::move(pin)};const auto used=uint32_t(bytes());auto peak=metrics.peak.load();while(used>peak&&!metrics.peak.compare_exchange_weak(peak,used)){}return true;
}
void PhotoImageCache::poll(const String& next,bool allowQuality,bool allowPrefetch){
    const auto revision=FileBrowserService::directoryRevision();
    if(!(revision==revision_)){const String target=target_;const auto edge=edge_;clear();revision_=revision;select(target,edge);}
    const bool pressure=heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<6*1024*1024||heap_caps_get_free_size(MALLOC_CAP_INTERNAL)<48*1024;
    if(pressure){for(auto& e:entries_)if(e.uri!=target_)e={};if(prefetch_&&pending_){pending_->canceled=true;pending_.reset();++metrics.cancelled;}if(sourceUri_!=target_){source_.reset();sourceUri_="";}allowPrefetch=false;}
    if(pending_&&prefetch_&&(pendingUri_!=next||!allowPrefetch)){pending_->canceled=true;pending_.reset();++metrics.cancelled;}
    bool completed=false;
    if(pending_&&pending_->ready){completed=true;auto request=pending_;pending_.reset();metrics.queue.add(request->queueMs);metrics.read.add(request->readMs);metrics.decode.add(request->decodeMs);
        if(request->image){const bool enough=std::max(request->sourceWidth,request->sourceHeight)<=pendingEdge_;const uint16_t quality=enough?edge_:pendingEdge_;
            if(retain(pendingUri_,quality,request->image)){if(pendingEdge_<edge_&&!enough){source_=request->source;sourceUri_=pendingUri_;}else if(sourceUri_==pendingUri_){source_.reset();sourceUri_="";}}
            else{++metrics.failures;if(!prefetch_){failed_=true;message_="Bildspeicher belegt; das sichtbare Bild bleibt erhalten.";}else failedNext_=pendingUri_;}
        }else{++metrics.failures;if(!prefetch_){failed_=true;message_=request->message;}else failedNext_=pendingUri_;}
    }
    if(auto pin=visible()){if(!first_){first_=true;metrics.first.add(millis()-selectedAt_);}if(final()&&!finished_){finished_=true;metrics.final.add(millis()-selectedAt_);}}
    // Let the UI bind/render a newly ready preview before another quality job.
    if(completed)return;
    if(pending_||!target_.length())return;
    if(!failed_&&!final()){
        if(!visible())start(target_,progressive(target_)?std::min<uint16_t>(edge_,ImageRenderPolicy::previewEdge):edge_,false);
        else if(allowQuality)start(target_,edge_,false);
        return;
    }
    if(allowPrefetch&&!pressure&&final()&&next.length()&&next!=target_&&next!=failedNext_){
        for(const auto& e:entries_)if(e.lease&&e.uri==next)return;
        const uint16_t quality=progressive(next)?std::min<uint16_t>(edge_,ImageRenderPolicy::previewEdge):edge_;
        const auto bounds=ImageRenderPolicy::fit(quality,quality,quality,quality);const size_t maximum=size_t(bounds.width)*bounds.height*2;
        if(bytes()+maximum<=budget)start(next,quality,true);
    }
}
String PhotoImageCache::metricsJson(){JsonDocument d;auto add=[&](const char* key,const LatencyHistogram& h){const auto v=h.snapshot();auto o=d[key].to<JsonObject>();o["samples"]=v.samples;o["p50_ms"]=v.p50Ms;o["p95_ms"]=v.p95Ms;o["max_ms"]=v.maxMs;};
    add("queue",metrics.queue);add("read",metrics.read);add("decode",metrics.decode);add("first_ready",metrics.first);add("final_ready",metrics.final);
    d["cache_hits"]=metrics.hits.load();d["cancellations"]=metrics.cancelled.load();d["failures"]=metrics.failures.load();d["cache_peak_bytes"]=metrics.peak.load();d["cache_budget_bytes"]=budget;d["pool_budget_bytes"]=ImageResourceService::kBudget;
    d["psram_free"]=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);d["psram_min"]=heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);d["heap_free"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);d["heap_largest"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    String text;serializeJson(d,text);return text;
}
}
