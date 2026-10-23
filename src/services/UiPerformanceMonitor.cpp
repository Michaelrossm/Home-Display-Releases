#include "UiPerformanceMonitor.h"
#include <esp_timer.h>
#include <algorithm>
#include <limits>
#include "../core/UiRefreshPolicy.h"

namespace hp {
UiPerformanceMonitor& UiPerformanceMonitor::instance(){static UiPerformanceMonitor monitor;return monitor;}

void UiPerformanceMonitor::Samples::add(uint32_t us){
    totalUs.fetch_add(us,std::memory_order_relaxed);
    const uint32_t ms=us/1000u+(us%1000u!=0);
    const uint32_t bucket=std::min(kBins-1,ms/widthMs);
    histogram[bucket].fetch_add(1,std::memory_order_relaxed);
    count.fetch_add(1,std::memory_order_relaxed);
    auto current=maxUs.load(std::memory_order_relaxed);
    while(us>current&&!maxUs.compare_exchange_weak(current,us,std::memory_order_relaxed)){}
}
UiPerformanceMonitor::Metric UiPerformanceMonitor::Samples::snapshot()const{
    Metric out{};
    uint32_t counts[kBins];uint64_t total=0;
    for(unsigned i=0;i<kBins;++i){counts[i]=histogram[i].load(std::memory_order_relaxed);total+=counts[i];}
    out.samples=static_cast<uint32_t>(std::min<uint64_t>(total,UINT32_MAX));
    out.maxUs=maxUs.load(std::memory_order_relaxed);
    if(!total)return out;
    out.meanUs=static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX,totalUs.load(std::memory_order_relaxed)/total));
    uint64_t seen=0;bool got50=false,got95=false,got99=false;
    for(unsigned i=0;i<kBins;++i){
        seen+=counts[i];
        // The last bucket is unbounded. Use the observed maximum rather than
        // reporting an artificially small percentile for slow app starts.
        const uint32_t upper=i==kBins-1
            ? out.maxUs/1000u+(out.maxUs%1000u!=0) : (i+1)*widthMs;
        if(!got50&&seen*100>=total*50){out.p50Ms=upper;got50=true;}
        if(!got95&&seen*100>=total*95){out.p95Ms=upper;got95=true;}
        if(!got99&&seen*100>=total*99){out.p99Ms=upper;got99=true;break;}
    }
    return out;
}

void UiPerformanceMonitor::attach(lv_display_t* display){
    if(!display||attached_.exchange(true,std::memory_order_acq_rel))return;
    display_=display;
    // Display events work with both BSP-owned and fallback LVGL handlers.
    lv_display_add_event_cb(display,displayEvent,LV_EVENT_ALL,this);
    // Existing touch driver remains the only input owner. This is an
    // observational callback, never sends gestures or modifies the UI.
    for(lv_indev_t* indev=lv_indev_get_next(nullptr);indev;indev=lv_indev_get_next(indev)){
        if(lv_indev_get_type(indev)==LV_INDEV_TYPE_POINTER){
            lv_indev_add_event_cb(indev,touchEvent,LV_EVENT_PRESSED,this);
            lv_indev_add_event_cb(indev,touchEvent,LV_EVENT_PRESSING,this);
        }
    }
}
void UiPerformanceMonitor::displayEvent(lv_event_t* ev){
    auto* self=static_cast<UiPerformanceMonitor*>(lv_event_get_user_data(ev));
    if(self&&lv_event_get_code(ev)==LV_EVENT_FLUSH_START){
        const auto* area=static_cast<const lv_area_t*>(lv_event_get_param(ev));
        if(area&&area->x2>=area->x1&&area->y2>=area->y1){
            const uint32_t pixels=uint32_t(area->x2-area->x1+1)*uint32_t(area->y2-area->y1+1);
            self->flushCount_.fetch_add(1,std::memory_order_relaxed);self->flushPixels_.fetch_add(pixels,std::memory_order_relaxed);
            auto peak=self->maxFlushPixels_.load();while(pixels>peak&&!self->maxFlushPixels_.compare_exchange_weak(peak,pixels)){}
        }
    }
    if(self)self->event(lv_event_get_code(ev));
}
void UiPerformanceMonitor::updateCadence(bool off,bool dimmed,bool saver,uint32_t lastActivity){
    if(!display_)return;auto* timer=lv_display_get_refr_timer(display_);if(!timer)return;
    const auto now=esp_timer_get_time();
    const auto touch=lastTouchUs_.load(std::memory_order_relaxed);
    const bool urgent=urgent_.exchange(false,std::memory_order_acq_rel);
    const bool interaction=urgent||(touch>0&&now-touch<500000)||uint32_t(millis()-lastActivity)<500||navigationPending();
    const uint32_t period=uiRefreshPeriod(off,dimmed,saver,interaction,lv_anim_count_running()>0);
    if(!period){if(!refreshPaused_){lv_timer_pause(timer);refreshPaused_=true;refreshPeriod_=0;}return;}
    const auto previous=refreshPeriod_.exchange(period);
    if(previous!=period)lv_timer_set_period(timer,period);
    if(refreshPaused_){refreshPaused_=false;lv_timer_resume(timer);lv_obj_invalidate(lv_display_get_screen_active(display_));lv_timer_ready(timer);}
    else if(urgent||(interaction&&previous!=period))lv_timer_ready(timer);
}
void UiPerformanceMonitor::touchEvent(lv_event_t* ev){
    auto* self=static_cast<UiPerformanceMonitor*>(lv_event_get_user_data(ev));
    if(self){self->lastTouchUs_.store(esp_timer_get_time(),std::memory_order_relaxed);
        if(self->display_&&!self->refreshPaused_&&self->refreshPeriod_.load()>33){
            if(auto* timer=lv_display_get_refr_timer(self->display_)){self->refreshPeriod_=33;lv_timer_set_period(timer,33);lv_timer_ready(timer);}
        }
    }
}
void UiPerformanceMonitor::event(lv_event_code_t code){
    const int64_t now=esp_timer_get_time();
    switch(code){
    case LV_EVENT_REFR_START:
        frameRendered_=false;
        break;
    case LV_EVENT_RENDER_START:
        renderStartUs_=now;
        break;
    case LV_EVENT_RENDER_READY:
        if(renderStartUs_>0&&now>=renderStartUs_)
            render_.add(static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,now-renderStartUs_)));
        renderStartUs_=0;
        frameRendered_=true;
        break;
    case LV_EVENT_FLUSH_START:
        flushStartUs_=now;
        break;
    case LV_EVENT_FLUSH_FINISH:
        if(flushStartUs_>0&&now>=flushStartUs_)
            flush_.add(static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,now-flushStartUs_)));
        flushStartUs_=0;
        break;
    case LV_EVENT_REFR_READY: {
        if(frameRendered_){
            if(meaningfulNavigation_.exchange(0,std::memory_order_acq_rel)>0){const auto requested=pendingNavigation_.load(std::memory_order_acquire);
                if(requested>0&&now>=requested)firstMeaningful_.add(static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,now-requested)));}
            renderedRefreshes_.fetch_add(1,std::memory_order_relaxed);
            ++windowFrames_;
            const auto built=builtNavigation_.exchange(0,std::memory_order_acq_rel);
            if(built>0){
                const auto requested=pendingNavigation_.exchange(0,std::memory_order_acq_rel);
                if(requested>0&&now>=requested)
                    visible_.add(static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,now-requested)));
            }
        }else emptyRefreshes_.fetch_add(1,std::memory_order_relaxed);
        const uint32_t nowMs=millis();
        if(!windowStartMs_)windowStartMs_=nowMs;
        const uint32_t elapsed=nowMs-windowStartMs_;
        if(elapsed>=1000){
            fps_.store(static_cast<uint32_t>(uint64_t(windowFrames_)*1000u/elapsed),std::memory_order_relaxed);
            windowFrames_=0;windowStartMs_=nowMs;
        }
        break;
    }
    default:break;
    }
}
void UiPerformanceMonitor::navigationQueued(bool preserveQueued){
    if(preserveQueued&&pendingNavigation_.load(std::memory_order_acquire)>0&&
       builtNavigation_.load(std::memory_order_acquire)==0)return;
    const int64_t now=esp_timer_get_time();
    const int64_t touch=lastTouchUs_.load(std::memory_order_relaxed);
    // A recent physical press is preferable to the later app-open call.
    // Programmatic navigation uses the call time instead.
    pendingNavigation_.store(touch>0&&now>=touch&&now-touch<1500000?touch:now,
                             std::memory_order_release);
    builtNavigation_.store(0,std::memory_order_release);
    meaningfulNavigation_.store(0,std::memory_order_release);
}
void UiPerformanceMonitor::navigationBuilt(){
    if(pendingNavigation_.load(std::memory_order_acquire)>0)
        builtNavigation_.store(esp_timer_get_time(),std::memory_order_release);
}
void UiPerformanceMonitor::navigationMeaningful(){if(pendingNavigation_.load(std::memory_order_acquire)>0)meaningfulNavigation_.store(esp_timer_get_time(),std::memory_order_release);}
void UiPerformanceMonitor::navigationFailed(){pendingNavigation_.store(0,std::memory_order_release);builtNavigation_.store(0,std::memory_order_release);}
void UiPerformanceMonitor::appBuild(uint32_t us,bool warm){build_.add(us);(warm?warm_:cold_).add(us);}
void UiPerformanceMonitor::luaRead(uint32_t us){read_.add(us);}
void UiPerformanceMonitor::luaCompile(uint32_t us){compile_.add(us);}
void UiPerformanceMonitor::homeBuild(uint32_t us){home_.add(us);}
UiPerformanceMonitor::Metric UiPerformanceMonitor::render() const{return render_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::flushCallback() const{return flush_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::appBuilds() const{return build_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::coldStarts() const{return cold_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::warmStarts() const{return warm_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::visibleNavigation() const{return visible_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::firstMeaningfulNavigation()const{return firstMeaningful_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::luaReads() const{return read_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::luaCompiles() const{return compile_.snapshot();}
UiPerformanceMonitor::Metric UiPerformanceMonitor::homeBuilds() const{return home_.snapshot();}
}
