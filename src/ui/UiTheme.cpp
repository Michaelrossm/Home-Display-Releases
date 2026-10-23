#include "UiTheme.h"
#include "../core/EventBus.h"
#include "../services/SettingsService.h"
#include "../services/ConfigService.h"

// Presentation selectors, joiners and skin-tone modifiers have no independent
// bitmap. This bounded monochrome font renders joined emoji as separate symbols.
extern "C" bool hp_font_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* glyph,
                                    uint32_t letter, uint32_t next) {
    if(letter==0xFE0E||letter==0xFE0F||letter==0x200D||(letter>=0x1F3FB&&letter<=0x1F3FF)) {
        const bool raw=glyph->req_raw_bitmap;*glyph={};glyph->req_raw_bitmap=raw;
        glyph->resolved_font=font;glyph->format=LV_FONT_GLYPH_FORMAT_A4;return true;
    }
    return lv_font_get_glyph_dsc_fmt_txt(font,glyph,letter,next);
}

namespace hp {

const lv_font_t* fontWithGermanFallback(const lv_font_t* base) {
    if (base == &lv_font_montserrat_14) return &hp_font_de_14;
    if (base == &lv_font_montserrat_18) return &hp_font_de_18;
    if (base == &lv_font_montserrat_24) return &hp_font_de_24;
    if (base == &lv_font_montserrat_28) return &hp_font_de_28;
    if (base == &lv_font_montserrat_32) return &hp_font_de_32;
    if (base == &lv_font_montserrat_48) return &hp_font_de_48;
    return base;
}

UiTheme& UiTheme::instance() {
    static UiTheme t;
    return t;
}

void UiTheme::setSelection(uint8_t mode, uint8_t accentIndex) {
    auto& catalog = ThemeCatalog::instance();
    catalog.begin();
    mode = catalog.normalizeMode(mode);
    if (accentIndex > 5) accentIndex = 0;
    if (mode_ == mode && accent_ == accentIndex && ready_) return;

    const Palette previous = active_;
    mode_ = mode;
    accent_ = accentIndex;
    active_ = catalog.palette(mode_, accent_);

    ++generation_;
    if (!generation_) generation_ = 1;
    EventBus::instance().publish({EventType::ThemeChanged});

    if (ready_) {
        applyLvglTheme();
        if (auto* screen = lv_screen_active()) recolorTree(screen, previous);
    }
}

void UiTheme::applyLvglTheme() {
    auto* display = lv_display_get_default();
    if (!display) return;

    auto* theme = lv_theme_default_init(
        display,
        accent(),
        surface(),
        dark() ? LV_THEME_DEFAULT_DARK : 0,
        &hp_font_de_14
    );
    lv_display_set_theme(display, theme);
}

void UiTheme::begin() {
    if (ready_) return;
    auto& catalog = ThemeCatalog::instance();
    catalog.begin();

    const auto settings = SettingsService::instance().snapshot();
    mode_ = catalog.normalizeMode(settings.ui.themeMode);
    if (settings.ui.themeMode >= ThemeCatalog::kBuiltInCount) {
        const String storedFile = ConfigService::instance().getString("ui_theme_file", "");
        const uint8_t resolved = catalog.modeForFilename(storedFile.c_str());
        mode_ = resolved != 0xFF ? resolved : 0;
    }
    accent_ = settings.ui.accent <= 5 ? settings.ui.accent : 0;
    active_ = catalog.palette(mode_, accent_);
    applyLvglTheme();
    ready_ = true;
}

void UiTheme::recolorTree(lv_obj_t* obj, const Palette& previous) {
    if (!obj || !lv_obj_is_valid(obj)) return;

    const auto mapColor = [&](lv_color_t value) {
        const uint32_t v = lv_color_to_u32(value) & 0xFFFFFFu;
        if (v == previous.background) return lv_color_hex(active_.background);
        if (v == previous.backgroundAlt) return lv_color_hex(active_.backgroundAlt);
        if (v == previous.surface) return lv_color_hex(active_.surface);
        if (v == previous.card) return lv_color_hex(active_.card);
        if (v == previous.border) return lv_color_hex(active_.border);
        if (v == previous.text) return lv_color_hex(active_.text);
        if (v == previous.textSecondary) return lv_color_hex(active_.textSecondary);
        if (v == previous.accent) return lv_color_hex(active_.accent);
        if (v == previous.onAccent) return lv_color_hex(active_.onAccent);
        if (v == previous.success) return lv_color_hex(active_.success);
        if (v == previous.warning) return lv_color_hex(active_.warning);
        if (v == previous.error) return lv_color_hex(active_.error);
        return value;
    };

    const auto oldBg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    const auto oldGrad = lv_obj_get_style_bg_grad_color(obj, LV_PART_MAIN);
    const auto oldText = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    const auto oldBorder = lv_obj_get_style_border_color(obj, LV_PART_MAIN);

    const auto newBg = mapColor(oldBg);
    const auto newGrad = mapColor(oldGrad);
    const auto newText = mapColor(oldText);
    const auto newBorder = mapColor(oldBorder);

    if ((lv_color_to_u32(oldBg) & 0xFFFFFFu) != (lv_color_to_u32(newBg) & 0xFFFFFFu))
        lv_obj_set_style_bg_color(obj, newBg, LV_PART_MAIN);
    if ((lv_color_to_u32(oldGrad) & 0xFFFFFFu) != (lv_color_to_u32(newGrad) & 0xFFFFFFu))
        lv_obj_set_style_bg_grad_color(obj, newGrad, LV_PART_MAIN);
    if ((lv_color_to_u32(oldText) & 0xFFFFFFu) != (lv_color_to_u32(newText) & 0xFFFFFFu))
        lv_obj_set_style_text_color(obj, newText, LV_PART_MAIN);
    if ((lv_color_to_u32(oldBorder) & 0xFFFFFFu) != (lv_color_to_u32(newBorder) & 0xFFFFFFu))
        lv_obj_set_style_border_color(obj, newBorder, LV_PART_MAIN);

    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        recolorTree(lv_obj_get_child(obj, i), previous);
}

lv_color_t UiTheme::background() const { return lv_color_hex(active_.background); }
lv_color_t UiTheme::backgroundAlt() const { return lv_color_hex(active_.backgroundAlt); }
lv_color_t UiTheme::surface() const { return lv_color_hex(active_.surface); }
lv_color_t UiTheme::card() const { return lv_color_hex(active_.card); }
lv_color_t UiTheme::border() const { return lv_color_hex(active_.border); }
lv_color_t UiTheme::textPrimary() const { return lv_color_hex(active_.text); }
lv_color_t UiTheme::textSecondary() const { return lv_color_hex(active_.textSecondary); }
lv_color_t UiTheme::accent() const { return lv_color_hex(active_.accent); }
lv_color_t UiTheme::onAccent() const { return lv_color_hex(active_.onAccent); }
lv_color_t UiTheme::success() const { return lv_color_hex(active_.success); }
lv_color_t UiTheme::warning() const { return lv_color_hex(active_.warning); }
lv_color_t UiTheme::error() const { return lv_color_hex(active_.error); }

void UiTheme::styleScreen(lv_obj_t* obj) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, background(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_grad_color(obj, backgroundAlt(), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, textPrimary(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

void UiTheme::styleTopBar(lv_obj_t* obj) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, surface(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_text_color(obj, textPrimary(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

void UiTheme::styleCard(lv_obj_t* obj, bool) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, card(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 22, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border(), 0);
    // Keep redraw regions bounded on the 1280x800 software-rendered UI.
    lv_obj_set_style_shadow_width(obj, 10, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 4, 0);
    lv_obj_set_style_text_color(obj, textPrimary(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

void UiTheme::styleInput(lv_obj_t* obj) const {
    if(!obj)return;
    lv_obj_set_style_bg_color(obj,backgroundAlt(),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(obj,textPrimary(),0);
    lv_obj_set_style_border_color(obj,border(),0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_border_color(obj,accent(),LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj,12,0);
    lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_set_style_pad_left(obj,14,0);
    lv_obj_set_style_pad_right(obj,14,0);
    lv_obj_set_style_text_color(obj,textSecondary(),LV_PART_TEXTAREA_PLACEHOLDER);
}

void UiTheme::styleRoundIcon(lv_obj_t* obj) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, backgroundAlt(), 0);
    lv_obj_set_style_bg_opa(obj, dark() ? LV_OPA_90 : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border(), 0);
    lv_obj_set_style_text_color(obj, textPrimary(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

void UiTheme::stylePrimaryButton(lv_obj_t* obj) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, accent(), 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, onAccent(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

void UiTheme::styleGlassPanel(lv_obj_t* obj) const {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, surface(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border(), 0);
    lv_obj_set_style_text_color(obj, textPrimary(), 0);
    lv_obj_set_style_text_font(obj, &hp_font_de_14, 0);
}

}
