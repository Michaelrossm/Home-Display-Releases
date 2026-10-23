#include "WallpaperService.h"
#include "LvFsBridge.h"
#include "UiTheme.h"
#include "../services/SettingsService.h"
#include "../services/StorageService.h"
#include "../services/PowerManager.h"
#include <algorithm>

namespace hp {

WallpaperService& WallpaperService::instance() {
    static WallpaperService w;
    return w;
}

bool WallpaperService::begin() {
    return LvFsBridge::instance().begin();
}

void WallpaperService::attach(lv_obj_t* parent) {
    parent_ = parent;
    if (!parent_) return;

    fallback_ = lv_obj_create(parent_);
    lv_obj_set_size(fallback_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(fallback_, 0, 0);
    lv_obj_clear_flag(fallback_, LV_OBJ_FLAG_SCROLLABLE);
    UiTheme::instance().styleScreen(fallback_);

    image_ = lv_image_create(parent_);
    lv_obj_set_size(image_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(image_, 0, 0);
    lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_CENTER);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);

    scrim_ = lv_obj_create(parent_);
    lv_obj_set_size(scrim_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(scrim_, 0, 0);
    lv_obj_clear_flag(scrim_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scrim_, 0, 0);
    lv_obj_set_style_bg_color(scrim_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim_, LV_OPA_TRANSP, 0);

    lv_obj_move_background(fallback_);
    lv_obj_move_foreground(scrim_);
    refresh();
}

String WallpaperService::lvglPath(const String& storagePath) const {
    if (!storagePath.length()) return {};
    return storagePath.startsWith("/")?String("sd:")+storagePath:storagePath;
}

void WallpaperService::showFallback() {
    usingImage_ = false;
    if(image_)lv_image_set_src(image_,nullptr);
    lv_image_cache_drop(&descriptor_);lv_image_header_cache_drop(&descriptor_);shown_.reset();
    if (scrim_) lv_obj_set_style_bg_opa(scrim_, LV_OPA_TRANSP, 0);
    if (fallback_) lv_obj_remove_flag(fallback_, LV_OBJ_FLAG_HIDDEN);
    if (image_) lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
}

void WallpaperService::applyScrim(uint8_t dimPercent) {
    if (!scrim_) return;
    if (dimPercent > 100) dimPercent = 100;
    const lv_opa_t opa = (lv_opa_t)((255u * dimPercent) / 100u);
    lv_obj_set_style_bg_opa(scrim_, opa, 0);
}

void WallpaperService::refresh() {
    if (!parent_ || !image_ || !fallback_) return;
    revision_=FileBrowserService::wallpaperRevision();
    if(pending_)pending_->canceled=true;pending_.reset();
    const auto s = SettingsService::instance().snapshot();
    applyScrim(s.ui.wallpaperDim);
    if (!s.ui.wallpaperEnabled || !s.ui.wallpaperPath.length()) {
        showFallback();
        return;
    }

    pending_=FileBrowserService::image(lvglPath(s.ui.wallpaperPath),lv_display_get_horizontal_resolution(nullptr),lv_display_get_vertical_resolution(nullptr));
}
bool WallpaperService::needsRefresh()const{return parent_&&(revision_!=FileBrowserService::wallpaperRevision()||(pending_&&pending_->ready.load(std::memory_order_acquire)));}
void WallpaperService::poll(){if(revision_!=FileBrowserService::wallpaperRevision())refresh();if(!pending_||!pending_->ready.load(std::memory_order_acquire))return;
    auto next=pending_->image;pending_.reset();if(!next){showFallback();return;}
    lv_image_set_src(image_,nullptr);lv_image_cache_drop(&descriptor_);lv_image_header_cache_drop(&descriptor_);shown_=std::move(next);descriptor_={};
    descriptor_.header.magic=LV_IMAGE_HEADER_MAGIC;descriptor_.header.cf=shown_->rgb565?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_ARGB8888;
    descriptor_.header.w=shown_->width;descriptor_.header.h=shown_->height;descriptor_.header.stride=shown_->width*(shown_->rgb565?2:4);descriptor_.data=shown_->pixels;descriptor_.data_size=descriptor_.header.stride*shown_->height;
    lv_image_set_src(image_,&descriptor_);usingImage_=true;lv_obj_remove_flag(image_,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(fallback_,LV_OBJ_FLAG_HIDDEN);
    applyScrim(SettingsService::instance().snapshot().ui.wallpaperDim);lv_obj_move_background(fallback_);lv_obj_move_to_index(image_,1);lv_obj_move_foreground(scrim_);
}

}
