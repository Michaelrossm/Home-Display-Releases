#include "DisplayManager.h"
#include "HomePanelConfig.h"
#include "../core/EventLog.h"
#include "../core/PerformancePolicy.h"
#include "../services/UiPerformanceMonitor.h"
#include "../services/PowerManager.h"
#include "../services/IdleService.h"
#include <algorithm>
#include <cstring>

#if HOMEDISPLAY_USE_WAVESHARE_BSP
#include "bsp/esp32_p4_platform.h"
#include "esp_lv_adapter_input.h"
#include <cmath>
#endif

namespace hp {

#if HOMEDISPLAY_USE_WAVESHARE_BSP
namespace {
// One read in the BSP's existing input path. Return its normal primary pointer,
// while retaining the second contact for gesture consumers. No app touches I2C.
esp_err_t readTouchWithPinch(esp_lcd_touch_handle_t handle,esp_lcd_touch_point_data_t* points,uint8_t* count,uint8_t capacity,void*){
    esp_lcd_touch_point_data_t raw[CONFIG_ESP_LCD_TOUCH_MAX_POINTS]{};uint8_t found=0;
    esp_err_t rc=esp_lcd_touch_read_data(handle);
    if(rc==ESP_OK)rc=esp_lcd_touch_get_data(handle,raw,&found,CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    uint16_t span=0;if(rc==ESP_OK&&found>=2){const float dx=float(raw[0].x)-raw[1].x,dy=float(raw[0].y)-raw[1].y;span=uint16_t(std::sqrt(dx*dx+dy*dy));}
    const uint8_t contacts=rc==ESP_OK?found:0;
    DisplayManager::instance().observeTouch(span,contacts,contacts?raw[0].x:0,contacts?raw[0].y:0,contacts>=2?(uint32_t(raw[0].x)+raw[1].x)/2:0,contacts>=2?(uint32_t(raw[0].y)+raw[1].y)/2:0);
    *count=rc==ESP_OK?std::min<uint8_t>(found,capacity):0;
    if(*count)memcpy(points,raw,*count*sizeof(*points));return rc;
}
}
#endif

DisplayManager& DisplayManager::instance() {
    static DisplayManager d;
    return d;
}

bool DisplayManager::begin() {
    if(initialized_)return true;
#if HOMEDISPLAY_USE_WAVESHARE_BSP
#if HOMEPANEL_FEATURE_POWER_MANAGEMENT
    performanceHeld_=PowerManager::instance().acquirePerformance();
    if(!performanceHeld_)return false;
#endif
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_90,
        // Preserve the verified P4 rotation/cache and triple-partial buffering.
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    cfg.lv_adapter_cfg.task_core_id = performance::kUiCore;
    display_ = bsp_display_start_with_config(&cfg);
    if (!display_) {
        if(performanceHeld_){PowerManager::instance().releasePerformance();performanceHeld_=false;}
        EventLog::instance().add(
            "ERROR", "DISPLAY_BSP", "Waveshare BSP display init failed"
        );
        return false;
    }

    bspManagesLvgl_ = true;
    touchReady_ = bsp_display_get_input_dev() != nullptr;
    initialized_ = true;
    Serial.println("[DISPLAY] Waveshare rotation=cpu tear=triple_partial");
    Serial.printf("[PERF] LVGL configured_core=%d IO configured_core=%d C6=adaptive-radio\n",
        performance::kUiCore,performance::kIoCore);

    // The observer attaches once to the EXISTING LVGL display. It never owns a
    // rendering task or modifies draw buffers. Failure to acquire the lock is
    // non-fatal; the UI/firmware must boot even without performance telemetry.
    if (bsp_display_lock(1000) == ESP_OK) {
        esp_lv_adapter_touch_callbacks_t callbacks{};callbacks.custom_touch_read=readTouchWithPinch;
        if(esp_lv_adapter_set_touch_callbacks(bsp_display_get_input_dev(),&callbacks)!=ESP_OK)
            EventLog::instance().add("WARN","TOUCH_GESTURE","Pinch observer unavailable");
        UiPerformanceMonitor::instance().attach(lv_display_get_default());
        bsp_display_unlock();
    } else {
        EventLog::instance().add("WARN","PERF_MONITOR","LVGL lock unavailable during attach");
    }

    if (bsp_display_backlight_on() != ESP_OK) {
        EventLog::instance().add("WARN", "DISPLAY_BACKLIGHT", "Backlight enable failed");
    }
    setBrightness(brightness_);

    EventLog::instance().add(
        "INFO", "DISPLAY_BSP",
        String("Waveshare JD9365/GT911 ready touch=") +
            (touchReady_ ? "1" : "0") +
            " tear=triple_partial"
    );
    return true;
#else
    lv_init();
    initialized_ = true;
    bspManagesLvgl_ = false;
    return true;
#endif
}

void DisplayManager::setBrightness(uint8_t value) {
#if HOMEDISPLAY_USE_WAVESHARE_BSP
    if (!initialized_) return;
    const int percent = (int)((uint32_t)value * 100u / 255u);
    if (percent == lastHardwarePercent_) {brightness_=value;return;}

    if (percent <= 0) {
        if(bsp_display_backlight_off()!=ESP_OK)return;
        // Backlight-off is NOT scanout-off: the BSP only changes brightness.
        // DSI still fetches PSRAM framebuffers, so retain the clock lock until
        // the display driver is actually stopped. Dropping to 40 MHz here
        // causes repeated LCD underruns even though the screen looks dark.
    } else {
#if HOMEPANEL_FEATURE_POWER_MANAGEMENT
        // Restore the verified render frequency BEFORE turning the panel on.
        if(!performanceHeld_){performanceHeld_=PowerManager::instance().acquirePerformance();if(!performanceHeld_)return;}
#endif
        if (lastHardwarePercent_ <= 0 && bsp_display_backlight_on()!=ESP_OK)return;
        if(bsp_display_brightness_set(percent)!=ESP_OK)return;
    }
    lastHardwarePercent_ = percent;
#endif
    brightness_ = value;
}

bool DisplayManager::lockUi(uint32_t timeoutMs) {
#if HOMEDISPLAY_USE_WAVESHARE_BSP
    if (bspManagesLvgl_) return bsp_display_lock(timeoutMs) == ESP_OK;
#endif
    return true;
}

void DisplayManager::unlockUi() {
#if HOMEDISPLAY_USE_WAVESHARE_BSP
    if (bspManagesLvgl_) bsp_display_unlock();
#endif
}

void DisplayManager::serviceLvgl() {
    const auto idle=IdleService::instance().stage();
    UiPerformanceMonitor::instance().updateCadence(idle==IdleStage::Off,idle==IdleStage::Dimmed,idle==IdleStage::Saver,IdleService::instance().lastActivityMillis());
    // Retry once the regular UI loop owns the BSP lock if early attach failed.
    if (bspManagesLvgl_ && !UiPerformanceMonitor::instance().attached())
        UiPerformanceMonitor::instance().attach(lv_display_get_default());
    if (!bspManagesLvgl_) lv_timer_handler();
}

}
