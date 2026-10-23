#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <atomic>

namespace hp {

class DisplayManager {
public:
    static DisplayManager& instance();

    bool begin();
    void setBrightness(uint8_t value);

    uint8_t brightness() const { return brightness_; }
    bool initialized() const { return initialized_; }
    bool touchReady() const { return touchReady_; }
    void reportTouchReady(bool value) { touchReady_ = value; }

    bool bspManagesLvgl() const { return bspManagesLvgl_; }
    bool lockUi(uint32_t timeoutMs = 1000);
    void unlockUi();
    void serviceLvgl();

    lv_display_t* display() const { return display_; }
    uint16_t pinchSpan()const {return uint32_t(millis()-touchUpdated_.load())<750?touchSpan_.load():0;}
    void observePinch(uint16_t span){touchSpan_.store(span);touchUpdated_.store(millis());}
    struct TouchSnapshot {uint16_t span=0,x=0,y=0,centerX=0,centerY=0;uint8_t count=0;uint32_t sequence=0,at=0;};
    void observeTouch(uint16_t span,uint8_t count,uint16_t x,uint16_t y,uint16_t cx,uint16_t cy){
        touchSequence_.fetch_add(1);touchXY_.store(uint32_t(x)|(uint32_t(y)<<16));touchCenter_.store(uint32_t(cx)|(uint32_t(cy)<<16));touchCount_.store(count);observePinch(span);touchSequence_.fetch_add(1);
    }
    TouchSnapshot touchSnapshot()const {TouchSnapshot out;const uint32_t before=touchSequence_.load();if(before&1)return out;const auto xy=touchXY_.load(),center=touchCenter_.load();out={touchSpan_.load(),uint16_t(xy),uint16_t(xy>>16),uint16_t(center),uint16_t(center>>16),touchCount_.load(),before,touchUpdated_.load()};
        if(before!=touchSequence_.load()||uint32_t(millis()-out.at)>=750)return {};return out;}

private:
    uint8_t brightness_ = 255;
    int lastHardwarePercent_ = -1;
    bool initialized_ = false;
    bool touchReady_ = false;
    bool bspManagesLvgl_ = false;
    bool performanceHeld_ = false;
    lv_display_t* display_ = nullptr;
    std::atomic<uint16_t> touchSpan_{0};std::atomic<uint32_t> touchUpdated_{0};
    std::atomic<uint32_t> touchSequence_{0},touchXY_{0},touchCenter_{0};std::atomic<uint8_t> touchCount_{0};
};

class DisplayUiGuard {
public:
    explicit DisplayUiGuard(uint32_t timeoutMs = 1000)
        : locked_(DisplayManager::instance().lockUi(timeoutMs)) {}
    ~DisplayUiGuard() {
        if (locked_) DisplayManager::instance().unlockUi();
    }
    bool locked() const { return locked_; }
    explicit operator bool() const { return locked_; }

private:
    bool locked_;
};

}
