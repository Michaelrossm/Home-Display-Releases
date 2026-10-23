#pragma once

#include <lvgl.h>

extern "C" {
extern const lv_font_t hp_font_de_14;
extern const lv_font_t hp_font_de_18;
extern const lv_font_t hp_font_de_24;
extern const lv_font_t hp_font_de_28;
extern const lv_font_t hp_font_de_32;
extern const lv_font_t hp_font_de_48;
}

namespace hp {

// All sizes contain Latin-1, punctuation, calculator symbols and common
// monochrome emoji. Not a full color emoji/complex shaping implementation.
// ASCII and LVGL icons fall back to the matching built-in Montserrat font.
const lv_font_t* fontWithGermanFallback(const lv_font_t* base);

} // namespace hp
