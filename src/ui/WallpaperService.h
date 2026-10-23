#pragma once
#include <lvgl.h>
#include <Arduino.h>
#include "../services/FileBrowserService.h"

namespace hp {

class WallpaperService {
public:
    static WallpaperService& instance();

    bool begin();
    void attach(lv_obj_t* parent);
    void refresh();
    void poll(); // Existing UiShell pass, under DisplayUiGuard.
    bool needsRefresh() const;
    bool usingImage() const { return usingImage_; }

private:
    String lvglPath(const String& storagePath) const;
    void showFallback();
    void applyScrim(uint8_t dimPercent);

    lv_obj_t* parent_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* fallback_ = nullptr;
    lv_obj_t* scrim_ = nullptr;
    bool usingImage_ = false;
    uint32_t revision_=0;
    std::shared_ptr<ImageRequest> pending_;
    std::shared_ptr<DecodedImage> shown_;
    lv_image_dsc_t descriptor_{};
};

}
