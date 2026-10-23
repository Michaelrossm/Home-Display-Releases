#include "BootSplash.h"
#include "BootSplashJpeg.h"
#include "../hardware/DisplayManager.h"
#include "../services/ImageCodec.h"
#include <lvgl.h>
#include <memory>

namespace hp {
namespace {
lv_obj_t* overlay = nullptr;
lv_obj_t* splash = nullptr;
lv_image_dsc_t descriptor = {};
std::shared_ptr<DecodedImage> decoded;

void renderNow() {
    // The BSP normally refreshes LVGL asynchronously. In early setup() the
    // regular desktop can replace the active screen before that first refresh.
    // Draw this one boot frame synchronously while holding the existing UI lock.
    if (auto* display = lv_display_get_default()) lv_refr_now(display);
}
}

bool BootSplash::show() {
    if (overlay) return true;
    if (!DisplayManager::instance().initialized()) return false;

    // Top layer survives lv_screen_load() in UiShell::begin(). Establish a
    // visible orange first frame before the slower JPEG decode and startup.
    {
        DisplayUiGuard guard(5000);
        if (!guard) return false;
        lv_obj_t* top = lv_layer_top();
        if (!top) return false;
        overlay = lv_obj_create(top);
        if (!overlay) return false;
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0xED7626), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_foreground(overlay);
        renderNow();
    }

    // ImageCodec is intentionally outside the LVGL lock. If JPEG decoding
    // fails, keep the orange overlay instead of reverting to a white screen.
    auto image = ImageCodec::decode(const_cast<uint8_t*>(bootasset::jpeg),
        bootasset::length, 1280, 800, false, nullptr, false, true);
    if (!image || !image->pixels || image->width != 1280 ||
        image->height != 800 || !image->rgb565) return true;

    DisplayUiGuard guard(5000);
    if (!guard || !overlay) return true;
    decoded = std::move(image);
    descriptor = {};
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.w = decoded->width;
    descriptor.header.h = decoded->height;
    descriptor.header.stride = decoded->width * 2;
    descriptor.data = decoded->pixels;
    descriptor.data_size = descriptor.header.stride * decoded->height;
    splash = lv_image_create(overlay);
    if (!splash) { decoded.reset(); descriptor = {}; return true; }
    lv_obj_remove_style_all(splash);
    lv_image_set_src(splash, &descriptor);
    lv_obj_center(splash);
    lv_obj_remove_flag(splash, LV_OBJ_FLAG_CLICKABLE);
    renderNow();
    return true;
}

void BootSplash::hide() {
    if (!overlay) return;
    DisplayUiGuard guard(5000);
    if (!guard) return;
    // Remove children first so LVGL never references the RGB565 descriptor
    // after its lifetime ends.
    if (splash) lv_image_set_src(splash, nullptr);
    lv_obj_delete(overlay);
    overlay = nullptr;
    splash = nullptr;
    if (decoded) {
        lv_image_cache_drop(&descriptor);
        lv_image_header_cache_drop(&descriptor);
        decoded.reset();
        descriptor = {};
    }
    renderNow();
}
}
