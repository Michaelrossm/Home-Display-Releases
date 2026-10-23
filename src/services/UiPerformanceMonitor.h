#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <atomic>
#include <cstdint>

namespace hp {
// Observes rendering and caps the existing refresh timer. Never owns the handler,
// never allocates in
// display callbacks, and does not write NVS/flash in a render callback.
class UiPerformanceMonitor {
public:
    struct Metric {
        uint32_t samples=0;
        uint32_t p50Ms=0, p95Ms=0, p99Ms=0, maxUs=0,meanUs=0;
    };
    static UiPerformanceMonitor& instance();
    void attach(lv_display_t* display); // Call under the existing LVGL lock.
    void updateCadence(bool off,bool dimmed,bool saver,uint32_t lastActivity); // Existing UI loop/lock only.
    void urgentUpdate(){urgent_.store(true,std::memory_order_release);}
    void widgetUpdate(bool changed){(changed?widgetChanges_:widgetSkips_).fetch_add(1,std::memory_order_relaxed);}
    uint32_t widgetChanges()const{return widgetChanges_.load();}
    uint32_t widgetSkips()const{return widgetSkips_.load();}
    uint32_t flushCount()const{return flushCount_.load();}
    uint64_t flushPixels()const{return flushPixels_.load();}
    uint32_t maxFlushPixels()const{return maxFlushPixels_.load();}
    uint32_t refreshPeriod()const{return refreshPeriod_.load();}
    void uiLoop(uint32_t us){loop_.add(us);}
    Metric uiLoops()const{return loop_.snapshot();}
    bool attached() const {return attached_.load(std::memory_order_acquire);}
    void navigationQueued(bool preserveQueued=false);
    void navigationBuilt();
    void navigationMeaningful();
    void navigationFailed();
    void appBuild(uint32_t us,bool warm=false);
    void luaRead(uint32_t us);
    void luaCompile(uint32_t us);
    void homeBuild(uint32_t us);
    Metric render() const;
    Metric flushCallback() const;
    Metric appBuilds() const;
    Metric coldStarts() const;
    Metric warmStarts() const;
    Metric visibleNavigation() const;
    Metric firstMeaningfulNavigation()const;
    Metric luaReads() const;
    Metric luaCompiles() const;
    Metric homeBuilds() const;
    uint32_t renderFps() const {return refreshPeriod_.load(std::memory_order_relaxed)?fps_.load(std::memory_order_relaxed):0;}
    uint32_t completedRefreshes() const {return renderedRefreshes_.load(std::memory_order_relaxed);}
    uint32_t noDrawRefreshes() const {return emptyRefreshes_.load(std::memory_order_relaxed);}
    uint32_t navigationPending() const {return pendingNavigation_.load(std::memory_order_relaxed)!=0;}
private:
    static constexpr uint32_t kBins=64;
    struct Samples {
        explicit Samples(uint32_t width):widthMs(width){}
        const uint32_t widthMs;
        std::atomic<uint32_t> count{0},maxUs{0};
        std::atomic<uint64_t> totalUs{0};
        std::atomic<uint32_t> histogram[kBins]{};
        void add(uint32_t elapsedUs);
        Metric snapshot() const;
    };
    static void displayEvent(lv_event_t* event);
    static void touchEvent(lv_event_t* event);
    void event(lv_event_code_t code);
    std::atomic<bool> attached_{false};
    lv_display_t* display_=nullptr;bool refreshPaused_=false;
    std::atomic<bool> urgent_{false};
    std::atomic<uint32_t> widgetChanges_{0},widgetSkips_{0},flushCount_{0},maxFlushPixels_{0},refreshPeriod_{0};
    std::atomic<uint64_t> flushPixels_{0};
    // All timestamps use esp_timer_get_time() and are only used within the
    // render callback (except navigation timestamps, which are atomic).
    int64_t renderStartUs_=0,flushStartUs_=0;
    bool frameRendered_=false;
    uint32_t windowStartMs_=0,windowFrames_=0;
    std::atomic<uint32_t> fps_{0}, renderedRefreshes_{0}, emptyRefreshes_{0};
    std::atomic<int64_t> pendingNavigation_{0},builtNavigation_{0},lastTouchUs_{0};
    std::atomic<int64_t> meaningfulNavigation_{0};
    Samples render_{2},flush_{2},build_{25},visible_{25},read_{10},compile_{10},home_{5};
    Samples cold_{25},warm_{5};
    Samples firstMeaningful_{25};
    Samples loop_{2};
};
}
