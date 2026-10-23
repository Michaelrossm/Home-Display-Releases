#pragma once
#include <lvgl.h>
#include <cstdint>
#include "UiFonts.h"
#include "ThemeCatalog.h"

namespace hp {

class UiTheme {
public:
    using Palette = ThemePalette;

    static UiTheme& instance();

    void setSelection(uint8_t mode, uint8_t accent);
    void begin();

    uint8_t mode() const { return mode_; }
    uint8_t accentIndex() const { return accent_; }
    uint16_t generation() const { return generation_; }
    bool dark() const { return active_.dark; }

    void styleScreen(lv_obj_t* obj) const;
    void styleTopBar(lv_obj_t* obj) const;
    void styleCard(lv_obj_t* obj, bool pressed = false) const;
    void styleInput(lv_obj_t* obj) const;
    void styleRoundIcon(lv_obj_t* obj) const;
    void stylePrimaryButton(lv_obj_t* obj) const;
    void styleGlassPanel(lv_obj_t* obj) const;

    lv_color_t background() const;
    lv_color_t backgroundAlt() const;
    lv_color_t surface() const;
    lv_color_t card() const;
    lv_color_t border() const;
    lv_color_t textPrimary() const;
    lv_color_t textSecondary() const;
    lv_color_t accent() const;
    lv_color_t onAccent() const;
    lv_color_t success() const;
    lv_color_t warning() const;
    lv_color_t error() const;

private:
    void recolorTree(lv_obj_t* obj, const Palette& previous);
    void applyLvglTheme();

    Palette active_{};
    bool ready_ = false;
    uint8_t mode_ = 0;
    uint8_t accent_ = 0;
    uint16_t generation_ = 1;
};

}
