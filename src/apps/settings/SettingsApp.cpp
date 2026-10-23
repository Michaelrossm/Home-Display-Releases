#include "SettingsApp.h"
#include <ArduinoJson.h>
#include "../../services/RegionService.h"
#include "DynamicAppSettings.h"
#include "BoardConfig.h"
#include "../../services/SettingsService.h"
#include "../../services/ConfigService.h"
#include "../../services/NetworkService.h"
#include "../../services/StorageService.h"
#include "../../services/IdleService.h"
#include "../../services/PowerManager.h"
#include "../../services/AudioHardwareService.h"
#include "HomePanelConfig.h"
#include "../../ui/UiTheme.h"
#include "../../ui/InputKeyboard.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/LicenseUi.h"
#include "../../ui/WallpaperService.h"
#include "../../ui/UiShell.h"
#include "../../ui/QrDialog.h"
#include "../../services/QrGenerator.h"
#include <mbedtls/platform_util.h>
#include "../files/FileExplorerApp.h"
#include "../../services/HardwareCapabilitiesService.h"
#include "../../services/AmbientLightService.h"
#include "../../services/BluetoothService.h"
#include "../../services/ConnectivityService.h"
#include "../../services/NasService.h"
#include "../../services/FileTransferService.h"
#include "../../hardware/DisplayManager.h"
#include "../../core/AppManager.h"
#include "../../core/EventLog.h"
#include "../../services/OtaService.h"
#include "../../services/C6UpdateService.h"
#include "../../services/FirmwareMediaUpdateService.h"
#include "../../services/OtaWriteSession.h"
#include "../../services/DiagnosticsService.h"
#include "../../ui/SecurityUi.h"
#include "../../ui/EmailAccountsUi.h"
#include "../../ui/StorageUsageUi.h"
#include "../../ui/AppIdleActionSettings.h"
#include "../../services/SecurityService.h"
#include "../../services/TimeService.h"
#include "../../services/MediaSessionService.h"

namespace hp {



static const char* hourOptions() {
    return "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
}

static const char* minuteOptions() {
    return "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
}

static const char* dayOptions() {
    return "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31";
}

static const char* monthOptions() {
    return "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12";
}

static const char* yearOptions() {
    return "2020\n2021\n2022\n2023\n2024\n2025\n2026\n2027\n2028\n2029\n2030\n2031\n2032\n2033\n2034\n2035\n2036\n2037\n2038\n2039\n2040\n2041\n2042\n2043\n2044\n2045\n2046\n2047\n2048\n2049\n2050";
}

static lv_obj_t* makeCompactDropdown(lv_obj_t* parent, const char* options, int width) {
    auto* dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_width(dd, width);
    lv_obj_set_height(dd, 46);
    SettingsWidgets::darkInput(dd);
    return dd;
}

static const char* powerLevelName(PowerLevel l) {
    switch (l) {
        case PowerLevel::FullSpeed: return "Volle Leistung";
        case PowerLevel::AdaptiveBurst: return "Adaptiver Burst";
        case PowerLevel::EcoCruise: return "Eco Cruise";
        case PowerLevel::DeepEco: return "Deep Eco";
        case PowerLevel::Performance: return "Performance-Boost";
    }
    return "?";
}


static void makeTransparent(lv_obj_t* obj) {
    SettingsWidgets::transparent(obj);
}

static lv_obj_t* makeSettingsContent(lv_obj_t* panel) {
    return SettingsWidgets::content(panel, true);
}

static lv_obj_t* makeSettingsRow(lv_obj_t* parent, int height = 58) {
    return SettingsWidgets::row(parent, height);
}

static lv_obj_t* makeSectionLabel(lv_obj_t* parent, const char* text) {
    return SettingsWidgets::section(parent, text);
}

static lv_obj_t* makeFieldLabel(lv_obj_t* parent, const char* text, int width = 300) {
    return SettingsWidgets::field(parent, text, width);
}


static void makeTextReadable(lv_obj_t* obj) {
    if (!obj) return;
    SettingsWidgets::readableText(obj);
}

static void styleSettingsInput(lv_obj_t* obj) {
    if (!obj) return;
    SettingsWidgets::darkInput(obj);
}

static uint8_t percentToBrightness(int percent) {
    percent = constrain(percent, 1, 100);
    return static_cast<uint8_t>((percent * 255 + 50) / 100);
}

static int brightnessToPercent(uint8_t value) {
    return constrain((static_cast<int>(value) * 100 + 127) / 255, 1, 100);
}

static void setPercentLabel(lv_obj_t* label, int value) {
    if (!label) return;
    String text = String(value) + " %";
    lv_label_set_text(label, text.c_str());
}

static void setSecondsLabel(lv_obj_t* label, int value) {
    if (!label) return;
    String text;
    if (value < 60) text = String(value) + " s";
    else text = String(value / 60) + " min " + String(value % 60) + " s";
    lv_label_set_text(label, text.c_str());
}

static void setMinutesLabel(lv_obj_t* label, int value) {
    if (!label) return;
    String text = String(value) + " min";
    lv_label_set_text(label, text.c_str());
}

static void styleSettingsNavButton(lv_obj_t* obj, bool selected) {
    SettingsWidgets::navButton(obj, selected);
}

bool SettingsApp::scrollBusy() const {
    return static_cast<int32_t>(scrollBusyUntil_ - millis()) > 0;
}

void SettingsApp::configureScrollSurface(lv_obj_t* obj) {
    if (!obj) return;
    // Tighter settings scrolling: no elastic bounce and no long momentum tail.
    // This reduces redraw work after the finger has already stopped.
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(obj, scrollPerfCb, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(obj, scrollPerfCb, LV_EVENT_SCROLL, this);
}

void SettingsApp::scrollPerfCb(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self) return;

    self->scrollBusyUntil_ = millis() + 140;
    self->keyboardPressValid_ = false;
    PowerManager::instance().requestBurst(900);

    if (!self->scrollSettleTimer_)
        self->scrollSettleTimer_ = lv_timer_create(scrollSettleCb, 80, self);
}

void SettingsApp::requestDeferredRefresh(bool wifi, bool ble, bool nas) {
    deferredWifiRefresh_ |= wifi;
    deferredBleRefresh_ |= ble;
    deferredNasRefresh_ |= nas;

    if (!scrollSettleTimer_)
        scrollSettleTimer_ = lv_timer_create(scrollSettleCb, 80, this);
}

void SettingsApp::scrollSettleCb(lv_timer_t* timer) {
    auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
    if (!self) {
        lv_timer_delete(timer);
        return;
    }
    if (self->scrollBusy()) return;

    // Delete before any rebuild, because rebuild may recreate large parts of UI.
    if (self->scrollSettleTimer_ == timer) self->scrollSettleTimer_ = nullptr;
    lv_timer_delete(timer);

    const bool wifi = self->deferredWifiRefresh_;
    const bool ble = self->deferredBleRefresh_;
    const bool nas = self->deferredNasRefresh_;
    self->deferredWifiRefresh_ = false;
    self->deferredBleRefresh_ = false;
    self->deferredNasRefresh_ = false;

    // Only touch the currently visible surface.
    if (wifi && self->root_ && self->wifiSetup_) {
        self->renderWifiNetworkList();
        return;
    }
    if (ble && self->root_ && self->page_ == Page::Bluetooth) {
        self->rebuild();
        return;
    }
    if (nas && self->root_ && self->nasBrowse_) {
        self->rebuild();
    }
}

void SettingsApp::setLaunchArguments(const String& value){JsonDocument args;if(!deserializeJson(args,value)&&args["view"]=="users"){const bool changed=page_!=Page::Users;page_=Page::Users;if(root_&&changed)rebuild();}}
void SettingsApp::open(lv_obj_t* p) {
    IdleService::instance().setContext(IdleContext::Normal);
    lv_obj_clean(p);
    root_ = lv_obj_create(p);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    // The launcher content and wallpaper beneath this app are transparent.
    // Give Settings its own opaque screen background so deleted Home widgets
    // can never show through during partial display refreshes.
    UiTheme::instance().styleScreen(root_);
    wifiProfileIndex_ = NetworkService::instance().activeWifiProfile();
    rebuild();
}

void SettingsApp::rebuild() {
    // Email uses the existing keyboard, not a second input architecture.
    if(keyboard_){lv_keyboard_set_textarea(keyboard_,nullptr);lv_obj_delete(keyboard_);keyboard_=nullptr;}
    if (!root_) return;
    // A settings page rebuild creates many LVGL objects. Keep the P4 at its
    // maximum supported clock only for this bounded UI operation, not while
    // the settings screen is idle. Guard is released on all return paths.
    PerformanceGuard uiBuildPerformance;

    usersView_.close();
    const bool modal = wifiSetup_ || nasSetup_ || nasBrowse_;
    const bool reusePage = !modal && pagePanel_ && sidebar_;
    keyboard_ = nullptr;
    wifiNetworksList_ = nullptr;
    keyboardNumeric_ = false;
    keyboardPressTarget_ = nullptr;
    keyboardPressValid_ = false;
    // Tear down only for modal screens and first opening. The 11-button
    // sidebar remains intact when changing normal settings categories.
    if (!reusePage) {
        pagePanel_ = nullptr;
        sidebar_ = nullptr;
        lastRenderedPage_ = 255;
        for (auto& button : navButtons_) button = nullptr;
        lv_obj_clean(root_);
        lv_obj_set_style_pad_all(root_, 12, 0);
        lv_obj_set_style_pad_column(root_, 12, 0);
        lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    } else {
        lv_obj_clean(pagePanel_);
        if (lastRenderedPage_ != static_cast<uint8_t>(page_)) {
            if (lastRenderedPage_ < 14 && navButtons_[lastRenderedPage_])
                styleSettingsNavButton(navButtons_[lastRenderedPage_], false);
            if (navButtons_[static_cast<uint8_t>(page_)])
                styleSettingsNavButton(navButtons_[static_cast<uint8_t>(page_)], true);
        }
    }

    if (wifiSetup_) { buildWifiSetup(); return; }
    if (nasSetup_) { buildNasSetup(); return; }
    if (nasBrowse_) { buildNasBrowser(); return; }

    const auto s = SettingsService::instance().snapshot();
    const auto ns = NetworkService::instance().ecoStatus();

    if (!reusePage) {
    auto* sidebar = lv_obj_create(root_);
    sidebar_ = sidebar;
    lv_obj_set_width(sidebar, 196);
    lv_obj_set_height(sidebar, LV_PCT(100));
    UiTheme::instance().styleGlassPanel(sidebar);
    lv_obj_set_scroll_dir(sidebar, LV_DIR_VER);
    configureScrollSurface(sidebar);
    lv_obj_set_style_pad_all(sidebar, 10, 0);

    auto* sideTitle = lv_label_create(sidebar);
    lv_label_set_text(sideTitle, "Einstellungen");
    lv_obj_set_style_text_font(
        sideTitle,
        fontWithGermanFallback(&lv_font_montserrat_18),
        0
    );
    lv_obj_align(sideTitle, LV_ALIGN_TOP_MID, 0, 4);

    const char* pages[] = {
        "Anzeige", "Energie", "Netzwerk", "Bluetooth", "Ton", "Speicher",
        "NAS", "Apps", "System", "Updates", "Diagnose", "Sicherheit", "E-Mail-Konten", "Benutzer"
    };

    int y = 44;
    for (uint8_t i = 0; i < sizeof(pages) / sizeof(*pages); ++i) {
        auto* button = lv_button_create(sidebar);
        lv_obj_set_size(button, 166, 46);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
        y += 52;

        styleSettingsNavButton(
            button,
            i == static_cast<uint8_t>(page_)
        );

        navButtons_[i] = button;
        lv_obj_add_event_cb(button, categoryCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(
            button,
            reinterpret_cast<void*>(static_cast<uintptr_t>(i))
        );

        auto* label = lv_label_create(button);
        lv_label_set_text(label, pages[i]);
        lv_obj_set_style_text_font(
            label,
            fontWithGermanFallback(&lv_font_montserrat_18),
            0
        );
        makeTextReadable(label);
        lv_obj_center(label);
    }

    }
    lastRenderedPage_ = static_cast<uint8_t>(page_);

    auto* panel = reusePage ? pagePanel_ : lv_obj_create(root_);
    if (!reusePage) pagePanel_ = panel;
    if (!reusePage) {
        lv_obj_set_height(panel, LV_PCT(100));
        lv_obj_set_flex_grow(panel, 1);
        UiTheme::instance().styleGlassPanel(panel);
        lv_obj_set_style_pad_all(panel, 22, 0);
        lv_obj_set_style_pad_row(panel, 16, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    }

    auto* title = lv_label_create(panel);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(
        title,
        fontWithGermanFallback(&lv_font_montserrat_28),
        0
    );

    makeTextReadable(panel);
    auto* content = makeSettingsContent(panel);
    makeTextReadable(content);
    configureScrollSurface(content);

    if(page_==Page::Users){
        lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_all(panel,8,0);
        lv_obj_set_style_pad_all(content,0,0);
        usersView_.openUsers(content);
    } else if (page_ == Page::Display) {
        lv_label_set_text(title, "Anzeige und Helligkeit");

        auto* info = lv_label_create(content);
        String text =
            "Helligkeit: " + String(s.brightness) + " / 255\n"
            "Dimmen nach: " + String(s.normal.dimAfter) + " s\n"
            "Display aus nach: " + String(s.normal.offAfter) + " s";
        lv_label_set_text(info, text.c_str());
        lv_obj_set_width(info, LV_PCT(100));
        lv_obj_set_style_text_font(
            info,
            fontWithGermanFallback(&lv_font_montserrat_18),
            0
        );

        auto* row = makeSettingsRow(content, 72);
        makeFieldLabel(row, "Helligkeit", 220);
        brightnessSlider_ = lv_slider_create(row);
        lv_obj_set_width(brightnessSlider_, 560);
        lv_obj_set_height(brightnessSlider_, 30);
        lv_slider_set_range(brightnessSlider_, 15, 255);
        lv_slider_set_value(brightnessSlider_, s.brightness, LV_ANIM_OFF);
        lv_obj_add_event_cb(
            brightnessSlider_,
            brightnessSliderChanged,
            LV_EVENT_VALUE_CHANGED,
            this
        );
        lv_obj_add_event_cb(
            brightnessSlider_,
            brightnessSliderChanged,
            LV_EVENT_RELEASED,
            this
        );
        makeSectionLabel(content,"Hintergrundbild");
        auto* wallpaperRow=makeSettingsRow(content,64);makeFieldLabel(wallpaperRow,"Eigenes Hintergrundbild",320);
        auto* wallpaperSwitch=lv_switch_create(wallpaperRow);if(s.ui.wallpaperEnabled)lv_obj_add_state(wallpaperSwitch,LV_STATE_CHECKED);
        lv_obj_add_event_cb(wallpaperSwitch,[](lv_event_t* e){const auto ui=SettingsService::instance().snapshot().ui;
            auto* sw=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!SettingsService::instance().setWallpaperControls(lv_obj_has_state(sw,LV_STATE_CHECKED),ui.wallpaperDim)){if(ui.wallpaperEnabled)lv_obj_add_state(sw,LV_STATE_CHECKED);else lv_obj_remove_state(sw,LV_STATE_CHECKED);}},LV_EVENT_VALUE_CHANGED,nullptr);
        auto* choose=lv_button_create(content);lv_obj_set_size(choose,360,58);auto* chooseText=lv_label_create(choose);lv_label_set_text(chooseText,"Hintergrundbild in Dateien auswählen");lv_obj_center(chooseText);
        lv_obj_add_event_cb(choose,[](lv_event_t*){if(UiShell::instance().requestAppOpen("files"))FileExplorerApp::requestWallpaperPicker();},LV_EVENT_CLICKED,nullptr);
        makeSectionLabel(content,(String("Ausgewählt: ")+s.ui.wallpaperPath).c_str());
        auto* modeRow=makeSettingsRow(content,64);makeFieldLabel(modeRow,"Darstellung beim nächsten Bildimport",400);
        auto* mode=lv_dropdown_create(modeRow);lv_dropdown_set_options(mode,"Füllen\nEinpassen\nZentriert (ohne Skalierung)");lv_dropdown_set_selected(mode,s.ui.wallpaperMode=="contain"?1:s.ui.wallpaperMode=="center"?2:0);
        lv_obj_add_event_cb(mode,[](lv_event_t* e){auto* object=static_cast<lv_obj_t*>(lv_event_get_target(e));const uint16_t value=lv_dropdown_get_selected(object);
            if(!SettingsService::instance().setWallpaperMode(value==1?"contain":value==2?"center":"cover")){const String previous=SettingsService::instance().snapshot().ui.wallpaperMode;lv_dropdown_set_selected(object,previous=="contain"?1:previous=="center"?2:0);}},LV_EVENT_VALUE_CHANGED,nullptr);
        auto* reset=lv_button_create(content);lv_obj_set_size(reset,360,52);auto* resetText=lv_label_create(reset);lv_label_set_text(resetText,"Hintergrund zurücksetzen");lv_obj_center(resetText);
        lv_obj_add_event_cb(reset,[](lv_event_t*){SettingsService::instance().setWallpaperControls(false,36);},LV_EVENT_CLICKED,nullptr);
        auto* dimRow=makeSettingsRow(content,64);makeFieldLabel(dimRow,"Bild abdunkeln (0–100 %)",300);auto* dim=lv_slider_create(dimRow);
        lv_obj_set_width(dim,400);lv_slider_set_range(dim,0,100);lv_slider_set_value(dim,s.ui.wallpaperDim,LV_ANIM_OFF);
        lv_obj_add_event_cb(dim,[](lv_event_t* e){const auto ui=SettingsService::instance().snapshot().ui;auto* slider=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!SettingsService::instance().setWallpaperControls(ui.wallpaperEnabled,lv_slider_get_value(slider)))lv_slider_set_value(slider,ui.wallpaperDim,LV_ANIM_OFF);},LV_EVENT_RELEASED,nullptr);
        makeSectionLabel(content,"PNG, JPEG, BMP, GIF, WebP, TGA. Vorschau und Import laufen im Hintergrund. Auch NAS-Bilder werden als lokale Kopie gespeichert.");
    } else if (page_ == Page::Energy) {
        lv_label_set_text(title, "Energiesparen");

        auto* intro = lv_label_create(content);
        lv_label_set_text(
            intro,
            "Vier Stufen bei Nichtbenutzung: Aktiv -> Dimmen -> Minimal -> Aus/Always-On."
        );
        lv_obj_set_width(intro, LV_PCT(100));
        lv_label_set_long_mode(intro, LV_LABEL_LONG_WRAP);

        auto makeSliderRow = [&](const char* labelText,
                                 lv_obj_t*& slider,
                                 lv_obj_t*& valueLabel,
                                 int minValue,
                                 int maxValue,
                                 int value) {
            auto* row = makeSettingsRow(content, 66);
            makeFieldLabel(row, labelText, 330);
            slider = lv_slider_create(row);
            lv_obj_set_width(slider, 390);
            lv_obj_set_height(slider, 26);
            lv_slider_set_range(slider, minValue, maxValue);
            lv_slider_set_value(slider, value, LV_ANIM_OFF);
            lv_obj_add_event_cb(slider, idleProfileChanged, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(slider, idleProfileChanged, LV_EVENT_RELEASED, this);
            valueLabel = lv_label_create(row);
            lv_obj_set_width(valueLabel, 135);
            makeTextReadable(valueLabel);
        };

        makeSectionLabel(content, "Stufe 1 - Aktiv");
        makeSliderRow("Normale Helligkeit", idleActiveBrightness_, idleActiveValue_, 5, 100, brightnessToPercent(s.brightness));
        setPercentLabel(idleActiveValue_, lv_slider_get_value(idleActiveBrightness_));

        makeSectionLabel(content, "Stufe 2 - Dimmen");
        makeSliderRow("Dimmen nach", idleDimAfter_, idleDimAfterValue_, 5, 900, s.normal.dimAfter);
        setSecondsLabel(idleDimAfterValue_, s.normal.dimAfter);
        makeSliderRow("Helligkeit Stufe 2", idleDimBrightness_, idleDimBrightnessValue_, 1, 100, brightnessToPercent(s.normal.dimBrightness));
        setPercentLabel(idleDimBrightnessValue_, brightnessToPercent(s.normal.dimBrightness));

        makeSectionLabel(content, "Stufe 3 - Minimal");
        makeSliderRow("Minimal nach", idleSaverAfter_, idleSaverAfterValue_, 10, 3600, s.normal.saverAfter);
        setSecondsLabel(idleSaverAfterValue_, s.normal.saverAfter);
        makeSliderRow("Helligkeit Stufe 3", idleSaverBrightness_, idleSaverBrightnessValue_, 1, 100, brightnessToPercent(s.normal.saverBrightness));
        setPercentLabel(idleSaverBrightnessValue_, brightnessToPercent(s.normal.saverBrightness));

        makeSectionLabel(content, "Stufe 4 - Aus / Always-On");
        makeSliderRow("Letzte Stufe nach", idleOffAfter_, idleOffAfterValue_, 1, 120, max<int>(1, s.normal.offAfter / 60));
        setMinutesLabel(idleOffAfterValue_, max<int>(1, s.normal.offAfter / 60));

        auto* aodRow = makeSettingsRow(content);
        makeFieldLabel(aodRow, "Always-On: nur Uhrzeit + Datum auf Schwarz", 430);
        alwaysOnSwitch_ = lv_switch_create(aodRow);
        if (s.normal.alwaysOnEnabled) lv_obj_add_state(alwaysOnSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(alwaysOnSwitch_, idleProfileChanged, LV_EVENT_VALUE_CHANGED, this);

        makeSliderRow("Always-On Helligkeit", alwaysOnBrightness_, alwaysOnBrightnessValue_, 1, 20, min<int>(20, brightnessToPercent(s.normal.alwaysOnBrightness)));
        setPercentLabel(alwaysOnBrightnessValue_, min<int>(20, brightnessToPercent(s.normal.alwaysOnBrightness)));
        addAppIdleActionSettings(content);

        auto* aodHint = lv_label_create(content);
        lv_label_set_text(
            aodHint,
            "Haken AUS: Stufe 4 schaltet die Hintergrundbeleuchtung vollstaendig aus. "
            "Haken AN: schwarzer Hintergrund mit ausschliesslich Uhrzeit und Datum."
        );
        lv_obj_set_width(aodHint, LV_PCT(100));
        lv_label_set_long_mode(aodHint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(aodHint, fontWithGermanFallback(&lv_font_montserrat_14), 0);

        auto* wakeRow = makeSettingsRow(content);
        makeFieldLabel(wakeRow, "Einmal tippen zum Aufwecken");
        wakeTapSwitch_ = lv_switch_create(wakeRow);
        if (SettingsService::instance().wakeOnSingleTap()) lv_obj_add_state(wakeTapSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(wakeTapSwitch_, wakeTapChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* energyRow = makeSettingsRow(content);
        makeFieldLabel(energyRow, "CPU-Energiesparen aktiv");
        energySwitch_ = lv_switch_create(energyRow);
        if (s.powerManagement.energySaving) lv_obj_add_state(energySwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(energySwitch_, energyChanged, LV_EVENT_VALUE_CHANGED, this);

        makeSectionLabel(content, "Netzwerk-Energiesparen");

        auto* row3 = makeSettingsRow(content);
        makeFieldLabel(row3, "Bei inaktivem Display WLAN ausschalten", 360);
        wifiScheduleSwitch_ = lv_switch_create(row3);
        if (s.networkEco.wifiScheduleEnabled) lv_obj_add_state(wifiScheduleSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(wifiScheduleSwitch_, wifiScheduleChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* lanRow = makeSettingsRow(content);
        makeFieldLabel(lanRow, "WLAN bei aktiver LAN-Verbindung ausschalten", 410);
        wifiOffOnLanSwitch_ = lv_switch_create(lanRow);
        if (s.networkEco.wifiOffWhenLan) lv_obj_add_state(wifiOffOnLanSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(wifiOffOnLanSwitch_, wifiEcoChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* timeRow = makeSettingsRow(content, 70);
        makeFieldLabel(timeRow, "WLAN-Zeitplan", 150);

        wifiScheduleStartHour_ = lv_dropdown_create(timeRow);
        styleSettingsInput(wifiScheduleStartHour_);
        lv_dropdown_set_options(wifiScheduleStartHour_, hourOptions());
        lv_dropdown_set_selected(wifiScheduleStartHour_, s.networkEco.wifiScheduleStartMinute / 60);
        lv_obj_set_width(wifiScheduleStartHour_, 88);
        lv_obj_add_event_cb(wifiScheduleStartHour_, wifiScheduleChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* colon1 = lv_label_create(timeRow);
        lv_label_set_text(colon1, ":");

        wifiScheduleStartMinute_ = lv_dropdown_create(timeRow);
        styleSettingsInput(wifiScheduleStartMinute_);
        lv_dropdown_set_options(wifiScheduleStartMinute_, minuteOptions());
        lv_dropdown_set_selected(wifiScheduleStartMinute_, s.networkEco.wifiScheduleStartMinute % 60);
        lv_obj_set_width(wifiScheduleStartMinute_, 88);
        lv_obj_add_event_cb(wifiScheduleStartMinute_, wifiScheduleChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* until = lv_label_create(timeRow);
        lv_label_set_text(until, "bis");

        wifiScheduleEndHour_ = lv_dropdown_create(timeRow);
        styleSettingsInput(wifiScheduleEndHour_);
        lv_dropdown_set_options(wifiScheduleEndHour_, hourOptions());
        lv_dropdown_set_selected(wifiScheduleEndHour_, s.networkEco.wifiScheduleEndMinute / 60);
        lv_obj_set_width(wifiScheduleEndHour_, 88);
        lv_obj_add_event_cb(wifiScheduleEndHour_, wifiScheduleChanged, LV_EVENT_VALUE_CHANGED, this);

        auto* colon2 = lv_label_create(timeRow);
        lv_label_set_text(colon2, ":");

        wifiScheduleEndMinute_ = lv_dropdown_create(timeRow);
        styleSettingsInput(wifiScheduleEndMinute_);
        lv_dropdown_set_options(wifiScheduleEndMinute_, minuteOptions());
        lv_dropdown_set_selected(wifiScheduleEndMinute_, s.networkEco.wifiScheduleEndMinute % 60);
        lv_obj_set_width(wifiScheduleEndMinute_, 88);
        lv_obj_add_event_cb(wifiScheduleEndMinute_, wifiScheduleChanged, LV_EVENT_VALUE_CHANGED, this);
    } else if (page_ == Page::Network) {
        lv_label_set_text(title, "Netzwerk");

        // Compact overview: two independent cards, no scrollable text wall.
        lv_obj_set_scroll_dir(content, LV_DIR_NONE);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_pad_row(content, 10, 0);
        lv_obj_set_style_pad_left(content, 0, 0);
        lv_obj_set_style_pad_right(content, 0, 0);

        auto& net = NetworkService::instance();
        const auto overview = net.overview();
        const auto& ws = overview.wifi;
        const auto& es = overview.ethernet;
        const bool lanUp = es.connected;
        const bool wifiUp = ws.connected;

        auto* headline = lv_obj_create(content);
        makeTransparent(headline);
        lv_obj_set_size(headline, LV_PCT(100), 34);
        lv_obj_set_flex_flow(headline, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(headline, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(headline, 12, 0);
        auto* online = lv_label_create(headline);
        const String summary = String(net.usingEthernet() ? "LAN aktiv" :
                                      (net.usingWifi() ? "WLAN aktiv" : "Offline")) +
                               "    |    " + s.hostname;
        lv_label_set_text(online, summary.c_str());
        SettingsWidgets::readableText(online, &lv_font_montserrat_18);
        lv_obj_set_width(online, LV_PCT(100));
        lv_label_set_long_mode(online, LV_LABEL_LONG_DOT);

        auto* cards = lv_obj_create(content);
        makeTransparent(cards);
        lv_obj_set_size(cards, LV_PCT(100), 374);
        lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cards, 10, 0);
        lv_obj_clear_flag(cards, LV_OBJ_FLAG_SCROLLABLE);

        auto makeCard = [&](const char* heading, bool connected) -> lv_obj_t* {
            auto* card = lv_obj_create(cards);
            lv_obj_set_width(card, LV_PCT(49));
            lv_obj_set_height(card, LV_PCT(100));
            UiTheme::instance().styleGlassPanel(card);
            lv_obj_set_style_radius(card, 16, 0);
            lv_obj_set_style_shadow_width(card, 0, 0);
            lv_obj_set_style_pad_all(card, 12, 0);
            lv_obj_set_style_pad_row(card, 5, 0);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            auto* headingLabel = lv_label_create(card);
            lv_label_set_text(headingLabel, heading);
            SettingsWidgets::readableText(headingLabel, &lv_font_montserrat_18);
            lv_obj_set_style_text_color(headingLabel,
                 connected ? lv_color_hex(0x7FE1B9) : lv_color_hex(0xD2DCE8), 0);
            lv_obj_set_height(headingLabel, 28);
            return card;
        };

        auto detail = [&](lv_obj_t* card, const char* caption,
                          const String& value) {
            auto* line = lv_obj_create(card);
            makeTransparent(line);
            lv_obj_set_size(line, LV_PCT(100), 24);
            lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(line, 8, 0);
            lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
            auto* key = lv_label_create(line);
            lv_label_set_text(key, caption);
            lv_obj_set_width(key, 126);
            lv_obj_set_style_text_font(key, fontWithGermanFallback(&lv_font_montserrat_14), 0);
            lv_obj_set_style_text_color(key, lv_color_hex(0xAFC4D9), 0);
            auto* val = lv_label_create(line);
            lv_label_set_text(val, value.length() ? value.c_str() : "-");
            lv_obj_set_flex_grow(val, 1);
            lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(val, fontWithGermanFallback(&lv_font_montserrat_14), 0);
            lv_obj_set_style_text_color(val, lv_color_hex(0xF5F7FA), 0);
        };

        auto* lan = makeCard(lanUp ? "LAN  |  Verbunden" : "LAN  |  Getrennt", lanUp);
        detail(lan, "Status", lanUp ? String("Verbunden") : String("Kein Link"));
        detail(lan, "IP-Adresse", lanUp ? es.ip : String("-"));
        detail(lan, "Subnetz", lanUp ? es.subnet : String("-"));
        detail(lan, "Gateway", lanUp ? es.gateway : String("-"));
        detail(lan, "DNS 1", lanUp ? es.dns1 : String("-"));
        detail(lan, "DNS 2", lanUp ? es.dns2 : String("-"));
        detail(lan, "MAC", es.mac);
        detail(lan, "Link", lanUp ? String(es.linkSpeedMbps) + " Mbit/s" : String("-"));

        auto* wlan = makeCard(wifiUp ? "WLAN  |  Verbunden" : "WLAN  |  Getrennt", wifiUp);
        detail(wlan, "C6 / Funk", String(ws.c6Ready ? "Bereit" : "Nicht bereit") +
                            " / " + (ws.enabled ? "Ein" : "Aus"));
        detail(wlan, "SSID", wifiUp ? ws.ssid : String("-"));
        detail(wlan, "IP-Adresse", wifiUp ? ws.ip : String("-"));
        detail(wlan, "Subnetz", wifiUp ? ws.subnet : String("-"));
        detail(wlan, "Gateway", wifiUp ? ws.gateway : String("-"));
        detail(wlan, "DNS 1", wifiUp ? ws.dns1 : String("-"));
        detail(wlan, "DNS 2", wifiUp ? ws.dns2 : String("-"));
        detail(wlan, "MAC", ws.c6Ready ? ws.mac : String("-"));
        detail(wlan, "Signal / Kanal", wifiUp ?
            String(ws.rssi) + " dBm / " + String(ws.channel) : String("-"));
        detail(wlan, "BSSID", wifiUp ? ws.bssid : String("-"));
        detail(wlan, "IP-Vergabe", wifiUp ? String(ws.staticIp ? "Statisch" : "DHCP") : String("-"));

        // The selected button itself conveys priority; no duplicate status line.
        auto* priorities = lv_obj_create(content);
        makeTransparent(priorities);
        lv_obj_set_size(priorities, LV_PCT(100), 48);
        lv_obj_set_flex_flow(priorities, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(priorities, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(priorities, 8, 0);
        lv_obj_clear_flag(priorities, LV_OBJ_FLAG_SCROLLABLE);
        auto* prompt = lv_label_create(priorities);
        lv_label_set_text(prompt, "Bevorzugtes WLAN");
        SettingsWidgets::readableText(prompt, &lv_font_montserrat_18);
        lv_obj_set_width(prompt, 200);
        bool savedAny = false;
        for (uint8_t slot = 0; slot < NetworkService::kMaxWifiProfiles; ++slot) {
            if (!net.wifiProfileConfigured(slot)) continue;
            savedAny = true;
            const bool preferred = net.preferredWifiProfile() == slot;
            auto* button = lv_button_create(priorities);
            lv_obj_set_size(button, 133, 40);
            styleSettingsNavButton(button, preferred);
            lv_obj_set_user_data(button,
                reinterpret_cast<void*>(static_cast<uintptr_t>(slot)));
            if (!preferred)
                lv_obj_add_event_cb(button, wifiOverviewPreferCb, LV_EVENT_CLICKED, this);
            auto* label = lv_label_create(button);
            const String name = "WLAN " + String(slot + 1) +
                                (preferred ? " (P)" : "");
            lv_label_set_text(label, name.c_str());
            SettingsWidgets::readableText(label, &lv_font_montserrat_18);
            lv_obj_center(label);
        }
        if (!savedAny) {
            auto* empty = lv_label_create(priorities);
            lv_label_set_text(empty, "Keine Profile gespeichert");
            SettingsWidgets::readableText(empty, &lv_font_montserrat_14);
        }

        auto* savedHeading=lv_label_create(content);lv_label_set_text(savedHeading,"Gespeicherte WLANs · Zugang per QR teilen");
        for(uint8_t slot=0;slot<NetworkService::kMaxWifiProfiles;++slot){
            if(!net.wifiProfileConfigured(slot))continue;
            auto* share=lv_button_create(content);lv_obj_set_size(share,LV_PCT(100),48);
            auto* text=lv_label_create(share);lv_label_set_text(text,(net.wifiProfileSsid(slot)+" · QR anzeigen").c_str());
            lv_obj_set_user_data(share,reinterpret_cast<void*>(uintptr_t(slot)));
            lv_obj_add_event_cb(share,[](lv_event_t* e){
                if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){SecurityUi::requestSettingsUnlock();return;}
                String ssid,password;const auto slot=uint8_t(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))));
                auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
                if(NetworkService::instance().getWifiProfile(slot,ssid,password)){String payload=QrGenerator::wifi(ssid,password);QrDialog::show(ssid.c_str(),payload,true,self->root_);if(payload.length())mbedtls_platform_zeroize(const_cast<char*>(payload.c_str()),payload.length());}
                if(password.length())mbedtls_platform_zeroize(const_cast<char*>(password.c_str()),password.length());
            },LV_EVENT_CLICKED,this);
        }
        auto* actions = lv_obj_create(content);
        makeTransparent(actions);
        lv_obj_set_size(actions, LV_PCT(100), 50);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(actions, 24, 0);
        lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

        auto* wifi = lv_button_create(actions);
        lv_obj_set_size(wifi, 224, 44);
        UiTheme::instance().stylePrimaryButton(wifi);
        lv_obj_add_event_cb(wifi, wifiSetupCb, LV_EVENT_CLICKED, this);
        auto* wl = lv_label_create(wifi);
        lv_label_set_text(wl, net.wifiAvailable() ? "WLAN einrichten" : "C6 nicht bereit");
        SettingsWidgets::readableText(wl, &lv_font_montserrat_18);
        lv_obj_center(wl);
        if (!net.wifiAvailable()) lv_obj_add_state(wifi, LV_STATE_DISABLED);

        auto* ecoLabel = lv_label_create(actions);
        lv_label_set_text(ecoLabel, "WLAN-Energiesparen");
        SettingsWidgets::readableText(ecoLabel, &lv_font_montserrat_18);
        wifiPowerSaveSwitch_ = lv_switch_create(actions);
        if (s.networkEco.wifiPowerSave)
            lv_obj_add_state(wifiPowerSaveSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(wifiPowerSaveSwitch_, wifiEcoChanged,
                            LV_EVENT_VALUE_CHANGED, this);

    } else if (page_ == Page::Bluetooth) {
        lv_label_set_text(title, "Bluetooth");
        auto& ble = BluetoothService::instance();
        const auto connectivity = ConnectivityService::snapshot();
        const auto& bleState = connectivity.bluetooth;

        auto* powerRow = makeSettingsRow(content, 64);
        makeFieldLabel(powerRow, "Bluetooth einschalten", 360);
        bleEnabledSwitch_ = lv_switch_create(powerRow);
        if (bleState.enabled) lv_obj_add_state(bleEnabledSwitch_, LV_STATE_CHECKED);
        lv_obj_add_event_cb(bleEnabledSwitch_, bleEnabledCb, LV_EVENT_VALUE_CHANGED, this);

        bleStatus_ = lv_label_create(content);
        lv_obj_set_width(bleStatus_, LV_PCT(100));
        lv_label_set_long_mode(bleStatus_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(bleStatus_, UiTheme::instance().textSecondary(), 0);
        lv_obj_set_style_text_font(bleStatus_, fontWithGermanFallback(&lv_font_montserrat_18), 0);
        String bleText = String("Status: ") + bleState.status +
            "\nESP32-C6: " + String(connectivity.c6Ready ? "bereit" : "nicht bereit") +
            " | WLAN: " + String(connectivity.wifiAvailable ? "verfuegbar" : "nicht verfuegbar") +
            " | BLE: " + String(connectivity.bleAvailable ? "bereit" : "nicht bereit") +
            "\nBluetooth-Modus: Low Energy (BLE); Classic-Audio / A2DP ist nicht verfuegbar.";
        lv_label_set_text(bleStatus_, bleText.c_str());

        auto* actionRow = makeSettingsRow(content, 64);
        auto* scan = lv_button_create(actionRow);
        lv_obj_set_size(scan, 320, 52);
        UiTheme::instance().stylePrimaryButton(scan);
        lv_obj_add_event_cb(scan, bleScanCb, LV_EVENT_CLICKED, this);
        auto* scanLabel = lv_label_create(scan);
        lv_label_set_text(scanLabel, !bleState.available ? "BLE nicht bereit" :
                          bleState.scanning ? "BLE-Suche laeuft ..." :
                          LV_SYMBOL_REFRESH " BLE-Geraete suchen");
        if (!bleState.available || bleState.scanning)
            lv_obj_add_state(scan, LV_STATE_DISABLED);
        makeTextReadable(scanLabel);
        lv_obj_center(scanLabel);

        bleList_ = lv_obj_create(content);
        lv_obj_set_width(bleList_, LV_PCT(100));
        lv_obj_set_height(bleList_, 372);
        lv_obj_set_style_bg_color(bleList_, lv_color_hex(0x101E30), 0);
        lv_obj_set_style_bg_opa(bleList_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bleList_, 0, 0);
        lv_obj_set_style_radius(bleList_, 16, 0);
        lv_obj_set_style_pad_all(bleList_, 12, 0);
        lv_obj_set_style_pad_row(bleList_, 10, 0);
        lv_obj_set_scroll_dir(bleList_, LV_DIR_VER);
        configureScrollSurface(bleList_);
        lv_obj_set_flex_flow(bleList_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(bleList_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        const size_t count = bleState.enabled ? bleState.deviceCount : 0;
        if (!count) {
            auto* empty = lv_label_create(bleList_);
            lv_obj_set_width(empty, LV_PCT(100));
            lv_label_set_text(empty,
                !bleState.enabled ? "Bluetooth ist ausgeschaltet." :
                bleState.available ? "Noch keine BLE-Geraete gefunden." :
                "Bluetooth konnte nicht initialisiert werden. Status oben pruefen.");
            lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(empty, UiTheme::instance().textSecondary(), 0);
            lv_obj_set_style_text_font(empty,
                fontWithGermanFallback(&lv_font_montserrat_18), 0);
        } else {
            for (size_t i = 0; i < count; ++i) {
                const auto dev = ble.device(i);
                auto* card = lv_obj_create(bleList_);
                lv_obj_set_width(card, LV_PCT(100));
                lv_obj_set_height(card, 90);
                lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_bg_color(card, lv_color_hex(0x1C3049), 0);
                lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(card, lv_color_hex(0x405B78), 0);
                lv_obj_set_style_border_width(card, 1, 0);
                lv_obj_set_style_shadow_width(card, 0, 0);
                lv_obj_set_style_radius(card, 10, 0);
                lv_obj_set_style_pad_all(card, 0, 0);
                lv_obj_set_style_text_color(card, UiTheme::instance().textPrimary(), 0);

                auto* name = lv_label_create(card);
                const char* displayName = dev.name[0] ? dev.name : "Unbekanntes BLE-Geraet";
                lv_label_set_text(name, displayName);
                lv_obj_set_width(name, LV_PCT(95));
                lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_color(name, UiTheme::instance().textPrimary(), 0);
                lv_obj_set_style_text_font(name,
                    fontWithGermanFallback(&lv_font_montserrat_24), 0);
                lv_obj_align(name, LV_ALIGN_TOP_LEFT, 16, 9);

                auto* details = lv_label_create(card);
                String meta = String(dev.address) + "   |   " +
                              String(static_cast<int>(dev.rssi)) + " dBm";
                lv_label_set_text(details, meta.c_str());
                lv_obj_set_width(details, LV_PCT(95));
                lv_label_set_long_mode(details, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_color(details, UiTheme::instance().textSecondary(), 0);
                lv_obj_set_style_text_font(details,
                    fontWithGermanFallback(&lv_font_montserrat_18), 0);
                lv_obj_align(details, LV_ALIGN_BOTTOM_LEFT, 16, -10);
            }
        }
    } else if (page_ == Page::Sound) {
        lv_label_set_text(title, "Ton");

        auto* info = lv_label_create(content);
        String text = "Systemlautstaerke: " + String(s.music.volume) + " %";
        lv_label_set_text(info, text.c_str());

        auto* row = makeSettingsRow(content, 72);
        makeFieldLabel(row, "Lautstaerke", 220);
        auto* slider = lv_slider_create(row);
        lv_obj_set_width(slider, 560);
        lv_obj_set_height(slider, 30);
        lv_slider_set_range(slider, 0, 100);
        lv_slider_set_value(slider, s.music.volume, LV_ANIM_OFF);
        lv_obj_add_event_cb(
            slider,
            volumeSliderChanged,
            LV_EVENT_VALUE_CHANGED,
            this
        );
        lv_obj_add_event_cb(
            slider,
            volumeSliderChanged,
            LV_EVENT_RELEASED,
            this
        );
    } else if (page_ == Page::Storage) {
        lv_label_set_text(title, "Speicher, SD und USB");

        StorageUsageUi::build(content);
        auto* danger=SettingsWidgets::content(content,false);lv_obj_set_flex_grow(danger,0);lv_obj_set_height(danger,LV_SIZE_CONTENT);SettingsWidgets::card(danger);lv_obj_set_style_pad_all(danger,18,0);
        SettingsWidgets::label(danger,"SD-Karte verwalten",0,&lv_font_montserrat_24);
        auto* formatStatus=lv_label_create(danger);SettingsWidgets::readableText(formatStatus);lv_label_set_text(formatStatus,StorageService::instance().sdFormatStatus());
        const auto confirmation=sdFormatConfirmation_.stage(millis());
        auto* warning=lv_label_create(danger);SettingsWidgets::readableText(warning);lv_obj_set_width(warning,LV_PCT(100));lv_label_set_long_mode(warning,LV_LABEL_LONG_WRAP);
        lv_label_set_text(warning,confirmation==1?
            "Bestaetigung 1 von 2: Alle Dateien im verwendeten SD-Dateisystem werden entfernt: Apps, Medien, Archive und fremde Dateien. Interner Flash bleibt unveraendert.":confirmation==2?
            "Bestaetigung 2 von 2: Endgueltig formatieren? Danach startet das Display neu und formatiert die SD-Karte. Keine Wiederherstellung in der Firmware. Kein sicheres Ueberschreiben. Karte NICHT wechseln, Strom NICHT unterbrechen!":
            "SD-Karte formatieren loescht alle Daten auf der Karte. Zwei weitere Bestaetigungen sind erforderlich; danach erfolgt ein Neustart.");
        auto* formatButton=lv_button_create(danger);lv_obj_set_width(formatButton,LV_PCT(100));
        lv_obj_t* formatLabel=lv_label_create(formatButton);lv_label_set_text(formatLabel,confirmation==1?"1/2: Ja, gesamte SD-Karte loeschen":confirmation==2?"2/2: Endgueltig loeschen und neu starten":"SD-Karte formatieren ...");
        if(!StorageService::instance().sdAvailable())lv_obj_add_state(formatButton,LV_STATE_DISABLED);
        lv_obj_add_event_cb(formatButton,sdFormatCb,LV_EVENT_CLICKED,this);
        if(confirmation){auto* cancel=lv_button_create(danger);lv_label_set_text(lv_label_create(cancel),"Abbrechen - nichts loeschen");lv_obj_add_event_cb(cancel,sdFormatCancelCb,LV_EVENT_CLICKED,this);}
    } else if (page_ == Page::Nas) {
        lv_label_set_text(title, "NAS und SMB-Dateien");
        auto* info = lv_label_create(content);
        lv_obj_set_style_text_color(info, UiTheme::instance().textPrimary(), 0);
        lv_label_set_text(info,
            "Unraid und FRITZ!Box: SMB2/3 - Ordner ansehen und Dateien auf SD kopieren. "
            "Musik kann als MP3/WAV heruntergeladen werden; Audiowiedergabe braucht "
            "noch den funktionsfaehigen Audio-BSP/PCM-Ausgang.");
        lv_obj_set_width(info,LV_PCT(100));
        lv_label_set_long_mode(info,LV_LABEL_LONG_WRAP);
        auto* addRow=makeSettingsRow(content,64);
        auto* add=lv_button_create(addRow);
        lv_obj_set_size(add,380,52);
        UiTheme::instance().stylePrimaryButton(add);
        lv_obj_add_event_cb(add,nasSetupCb,LV_EVENT_CLICKED,this);
        auto* al=lv_label_create(add);
        lv_label_set_text(al,"SMB / WebDAV-Profil hinzufuegen");
        makeTextReadable(al);lv_obj_center(al);
        auto& ns=NasService::instance();
        for(size_t i=0;i<ns.profileCount();++i){
            const NasProfile* p=ns.profile(i);
            if(!p)continue;
            auto* row=makeSettingsRow(content,62);
            auto* btn=lv_button_create(row);
            lv_obj_set_size(btn,780,55);
            UiTheme::instance().styleGlassPanel(btn);
            lv_obj_set_user_data(btn,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
            if(p->protocol==NasProtocol::Smb2)
                lv_obj_add_event_cb(btn,nasOpenCb,LV_EVENT_CLICKED,this);
            else lv_obj_add_state(btn,LV_STATE_DISABLED);
            auto* label=lv_label_create(btn);
            String text=String(p->name)+" | "+String(p->host)+" | "+
                        (p->protocol==NasProtocol::Smb2?"SMB - Oeffnen":"WebDAV - Profil gespeichert");
            lv_label_set_text(label,text.c_str());
            lv_obj_set_style_text_color(label,UiTheme::instance().textPrimary(),0);
            lv_obj_center(label);
        }
    } else if (page_ == Page::Apps) {
        DynamicAppSettings::instance().invalidate();
        lv_label_set_text(title, "App-Einstellungen");

        auto* info = lv_label_create(content);
        lv_label_set_text(
            info,
            "App-spezifische Einstellungen werden von der jeweiligen "
            "Lua-App bzw. Web-App bereitgestellt."
        );
        lv_obj_set_width(info, LV_PCT(100));
        lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    } else if (page_ == Page::System) {
        lv_label_set_text(title, "System und Diagnose");
        LicenseUi::build(content);
        makeSectionLabel(content,"Region");
        auto* countryRow=makeSettingsRow(content,58);makeFieldLabel(countryRow,"Standardland",200);
        auto* country=lv_dropdown_create(countryRow);String countryOptions;
        for(size_t i=0;i<RegionService::count();++i){if(i)countryOptions+='\n';countryOptions+=RegionService::name(i);}
        lv_dropdown_set_options(country,countryOptions.c_str());lv_dropdown_set_selected(country,RegionService::instance().selected());lv_obj_set_width(country,340);SettingsWidgets::darkInput(country);
        lv_obj_add_event_cb(country,[](lv_event_t* e){auto* dropdown=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!RegionService::instance().request(lv_dropdown_get_selected(dropdown)))lv_dropdown_set_selected(dropdown,RegionService::instance().selected());},LV_EVENT_VALUE_CHANGED,nullptr);
        auto* regionStatus=SettingsWidgets::label(content,RegionService::instance().status(),0);
        lv_obj_set_user_data(regionStatus,country);
        auto* regionTimer=lv_timer_create([](lv_timer_t* t){auto* label=static_cast<lv_obj_t*>(lv_timer_get_user_data(t));auto& service=RegionService::instance();const char* text=service.status();if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);auto* picker=static_cast<lv_obj_t*>(lv_obj_get_user_data(label));if(!service.busy()&&lv_dropdown_get_selected(picker)!=service.selected())lv_dropdown_set_selected(picker,service.selected());},500,regionStatus);
        lv_obj_add_event_cb(regionStatus,[](lv_event_t* e){lv_timer_delete(static_cast<lv_timer_t*>(lv_event_get_user_data(e)));},LV_EVENT_DELETE,regionTimer);

        makeSectionLabel(content, tr("language.title", "Sprache"));
        auto* languageRow=makeSettingsRow(content,58);
        makeFieldLabel(languageRow,tr("language.package","Sprachpaket"),200);
        auto* language=lv_dropdown_create(languageRow);String options;auto& languages=LanguageService::instance();uint32_t selected=0;
        for(size_t i=0;i<languages.count();++i){if(i)options+='\n';options+=languages.name(i);if(!strcmp(languages.id(i),languages.requested()))selected=i;}
        lv_dropdown_set_options(language,options.c_str());lv_dropdown_set_selected(language,selected);lv_obj_set_width(language,300);SettingsWidgets::darkInput(language);
        auto* languageButton=lv_button_create(languageRow);UiTheme::instance().stylePrimaryButton(languageButton);
        auto* languageCaption=lv_label_create(languageButton);lv_label_set_text(languageCaption,tr("language.save","Sprache speichern"));lv_obj_center(languageCaption);
        auto* languageStatus=SettingsWidgets::label(content,languages.count()==1?tr("language.german_only","Derzeit ist nur Deutsch verfügbar."):tr("language.available","Verfügbare Sprachen"),0);lv_obj_set_width(languageStatus,LV_PCT(100));
        SettingsWidgets::label(content,tr("language.compressed"),0);
        SettingsWidgets::label(content,tr("language.restart","Sprachwechsel werden nach einem Neustart aktiviert."),0);
        lv_obj_set_user_data(language,languageStatus);
        lv_obj_add_event_cb(languageButton,[](lv_event_t* e){auto* dropdown=static_cast<lv_obj_t*>(lv_event_get_user_data(e));auto& service=LanguageService::instance();
            const bool ok=service.request(service.id(lv_dropdown_get_selected(dropdown)));auto* status=static_cast<lv_obj_t*>(lv_obj_get_user_data(dropdown));
            lv_label_set_text(status,ok?service.status():tr("language.error"));},LV_EVENT_CLICKED,language);

        auto* info = lv_label_create(content);
        String text =
            "Geraetename: " + s.hostname +
            "\nDisplay/Touch: " +
            String(DisplayManager::instance().touchReady()
                ? "bereit"
                : "nicht verfuegbar") +
            "\nSpeicherkarte: " +
            String(StorageService::instance().sdAvailable()
                ? "bereit"
                : "nicht vorhanden") +
            "\nNetzwerk: " +
            String(NetworkService::instance().online()
                ? "online"
                : "offline");
        lv_label_set_text(info, text.c_str());
        lv_obj_set_width(info, LV_PCT(100));
        lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

        makeSectionLabel(content, "Datum und Uhrzeit");

        auto* current = lv_label_create(content);
        String currentText =
            "Aktuell: " + TimeService::instance().dateText() +
            "  " + TimeService::instance().timeText();
        const char* source = TimeService::instance().sourceName();
        if (strcmp(source, "network") == 0) currentText += "  | automatisch synchronisiert";
        else if (strcmp(source, "manual") == 0) currentText += "  | manuell";
        else if (strcmp(source, "restored") == 0) currentText += "  | letzter gespeicherter Stand";
        else currentText += "  | noch keine gueltige Zeit";
        lv_label_set_text(current, currentText.c_str());
        SettingsWidgets::readableText(current, &lv_font_montserrat_18);

        auto* timeRow = makeSettingsRow(content, 58);
        makeFieldLabel(timeRow, "Manuell setzen", 150);

        timeDay_ = makeCompactDropdown(timeRow, dayOptions(), 82);
        timeMonth_ = makeCompactDropdown(timeRow, monthOptions(), 82);
        timeYear_ = makeCompactDropdown(timeRow, yearOptions(), 112);
        timeHour_ = makeCompactDropdown(timeRow, hourOptions(), 82);
        timeMinute_ = makeCompactDropdown(timeRow, minuteOptions(), 82);

        // Preselect current local date/time when available.
        const time_t nowEpoch = TimeService::instance().nowUtc();
        if (nowEpoch > 1700000000) {
            struct tm nowLocal{};
            if (TimeService::instance().localFromEpoch(nowEpoch, nowLocal)) {
                lv_dropdown_set_selected(timeDay_, nowLocal.tm_mday - 1);
                lv_dropdown_set_selected(timeMonth_, nowLocal.tm_mon);
                const int year = nowLocal.tm_year + 1900;
                lv_dropdown_set_selected(timeYear_, constrain(year, 2020, 2050) - 2020);
                lv_dropdown_set_selected(timeHour_, nowLocal.tm_hour);
                lv_dropdown_set_selected(timeMinute_, nowLocal.tm_min);
            }
        } else {
            lv_dropdown_set_selected(timeYear_, 2026 - 2020);
        }

        auto* setButton = lv_button_create(timeRow);
        lv_obj_set_size(setButton, 155, 46);
        UiTheme::instance().stylePrimaryButton(setButton);
        lv_obj_add_event_cb(setButton, manualTimeSetCb, LV_EVENT_CLICKED, this);
        auto* setLabel = lv_label_create(setButton);
        lv_label_set_text(setLabel, "Uebernehmen");
        lv_obj_center(setLabel);

        timeManualStatus_ = lv_label_create(content);
        lv_label_set_text(
            timeManualStatus_,
            "Ohne Internet laeuft die manuell gesetzte Uhr weiter. "
            "Sobald NTP wieder erreichbar ist, wird sie automatisch korrigiert."
        );
        lv_obj_set_width(timeManualStatus_, LV_PCT(100));
        lv_label_set_long_mode(timeManualStatus_, LV_LABEL_LONG_WRAP);
        SettingsWidgets::readableText(timeManualStatus_, &lv_font_montserrat_14);

        makeSectionLabel(content, "Systemstatus");
        auto* eventStatus = lv_label_create(content);
        const uint32_t warnings = EventLog::instance().warningCount();
        const uint32_t errors = EventLog::instance().errorCount();
        String eventText = "Warnungen: " + String(warnings) + " | Fehler: " + String(errors);
        const String recentEvents = EventLog::instance().recentText(4, true);
        if (recentEvents.length()) eventText += "\n" + recentEvents;
        else eventText += "\nKeine aktuellen Warnungen oder Fehler protokolliert.";
        lv_label_set_text(eventStatus, eventText.c_str());
        lv_obj_set_width(eventStatus, LV_PCT(100));
        lv_label_set_long_mode(eventStatus, LV_LABEL_LONG_WRAP);
        SettingsWidgets::readableText(eventStatus, &lv_font_montserrat_14);
    } else if (page_ == Page::Security) {
        lv_label_set_text(title,"Sicherheit");
        SecurityUi::buildPage(content);
    } else if (page_ == Page::Email) {
        lv_label_set_text(title,"E-Mail-Konten");
        EmailAccountsUi::build(content,textareaFocusCb,this);
        createKeyboard();
    } else if (page_ == Page::Updates) {
        lv_label_set_text(title, "Updates und Wiederherstellung");
        auto* info = lv_label_create(content);
        String text =
            "P4-OTA-Status: " +
            String(static_cast<int>(OtaService::instance().state())) +
            "\nC6: " + C6UpdateService::instance().status() +
            "\n\nDer C6-Updateknopf uebertraegt die projektgebundene "
            "Home-Display-C6-Firmware auf Basis von ESP-Hosted 2.12.13 "
            "ueber SDIO.";
        lv_label_set_text(info, text.c_str());
        lv_obj_set_width(info, LV_PCT(100));
        lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

        auto* row = makeSettingsRow(content, 64);
        auto* update = lv_button_create(row);
        lv_obj_set_size(update, 390, 52);
        UiTheme::instance().stylePrimaryButton(update);
        lv_obj_add_event_cb(
            update,
            c6UpdateCb,
            LV_EVENT_CLICKED,
            this
        );
        auto* label = lv_label_create(update);
        lv_label_set_text(
            label,
            C6UpdateService::instance().inProgress()
                ? "C6-Update laeuft ..."
                : "C6 ueber SDIO aktualisieren"
        );
        lv_obj_center(label);

        if (C6UpdateService::instance().inProgress() ||
            !NetworkService::instance().online()) {
            lv_obj_add_state(update, LV_STATE_DISABLED);
        }
        auto& mediaUpdater=FirmwareMediaUpdateService::instance();
        makeSectionLabel(content,"Lokale Firmware von SD / USB");
        auto* mediaInfo=lv_label_create(content);
        const String mediaText="Ordner: /Firmware\n"+mediaUpdater.storedInfo()+"\n"+mediaUpdater.status()+
            "\nP4: Firmware-P4.bin · C6: Firmware-C6.bin";
        lv_label_set_text(mediaInfo,mediaText.c_str());
        lv_obj_set_width(mediaInfo,LV_PCT(100));
        lv_label_set_long_mode(mediaInfo,LV_LABEL_LONG_WRAP);
        SettingsWidgets::readableText(mediaInfo,&lv_font_montserrat_14);
        auto makeMediaButton=[&](const char* labelText,lv_event_cb_t cb){
            auto* r=makeSettingsRow(content,56);
            auto* b=lv_button_create(r);
            lv_obj_set_width(b,LV_PCT(100));lv_obj_set_height(b,48);
            UiTheme::instance().stylePrimaryButton(b);
            lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,this);
            auto* caption=lv_label_create(b);lv_label_set_text(caption,labelText);lv_obj_center(caption);
            if(!mediaUpdater.available()||mediaUpdater.busy())lv_obj_add_state(b,LV_STATE_DISABLED);
        };
        makeMediaButton(mediaInstallConfirm_==1?"P4-Installation bestätigen":"P4-Datei suchen und lokal installieren",mediaP4UpdateCb);
        makeMediaButton(mediaInstallConfirm_==2?"C6-Installation bestätigen":"C6-Datei suchen und lokal installieren",mediaC6UpdateCb);
        if(mediaUpdater.status().startsWith("P4-Firmware geprüft")){
            auto* restartRow=makeSettingsRow(content,54);
            auto* restart=lv_button_create(restartRow);
            lv_obj_set_width(restart,LV_PCT(100));lv_obj_set_height(restart,48);
            lv_obj_add_event_cb(restart,mediaRebootCb,LV_EVENT_CLICKED,this);
            auto* text=lv_label_create(restart);
            lv_label_set_text(text,mediaRebootConfirm_?"P4-Neustart bestätigen":"P4-Firmware aktivieren: Neustart");lv_obj_center(text);
        }
        auto* refreshRow=makeSettingsRow(content,48);
        auto* refresh=lv_button_create(refreshRow);
        lv_obj_set_size(refresh,200,44);
        lv_obj_add_event_cb(refresh,mediaRefreshCb,LV_EVENT_CLICKED,this);
        auto* refreshText=lv_label_create(refresh);
        lv_label_set_text(refreshText,"Status aktualisieren");lv_obj_center(refreshText);
    } else {
        lv_label_set_text(title, "Diagnose");

        auto* report = lv_textarea_create(content);
        lv_obj_set_width(report, LV_PCT(100));
        lv_obj_set_height(report, 500);
        lv_textarea_set_one_line(report, false);
        lv_textarea_set_text(
            report,
            DiagnosticsService::instance().supportReportText().c_str()
        );
    }
}

void SettingsApp::manualTimeSetCb(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self || !self->timeDay_ || !self->timeMonth_ || !self->timeYear_ ||
        !self->timeHour_ || !self->timeMinute_) return;

    const uint8_t day = static_cast<uint8_t>(lv_dropdown_get_selected(self->timeDay_) + 1);
    const uint8_t month = static_cast<uint8_t>(lv_dropdown_get_selected(self->timeMonth_) + 1);
    const uint16_t year = static_cast<uint16_t>(2020 + lv_dropdown_get_selected(self->timeYear_));
    const uint8_t hour = static_cast<uint8_t>(lv_dropdown_get_selected(self->timeHour_));
    const uint8_t minute = static_cast<uint8_t>(lv_dropdown_get_selected(self->timeMinute_));

    const bool ok = TimeService::instance().setManualLocal(year, month, day, hour, minute);
    if (self->timeManualStatus_) {
        if (ok) {
            String status =
                "Gesetzt: " + TimeService::instance().dateText() +
                "  " + TimeService::instance().timeText() +
                ". NTP bleibt im Hintergrund aktiv.";
            lv_label_set_text(self->timeManualStatus_, status.c_str());
        } else {
            lv_label_set_text(
                self->timeManualStatus_,
                "Ungueltiges Datum. Bitte Tag, Monat und Jahr pruefen."
            );
        }
    }
    IdleService::instance().activity();
}

void SettingsApp::mediaRebootCb(lv_event_t* e){
 auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;
 auto& updater=FirmwareMediaUpdateService::instance();
 if(updater.busy()||!updater.status().startsWith("P4-Firmware geprüft"))return;
 if(!self->mediaRebootConfirm_){self->mediaRebootConfirm_=true;self->rebuild();return;}
 std::unique_lock<std::mutex> otaLock(OtaWriteSession::mutex(),std::try_to_lock);
 if(!otaLock.owns_lock())return;
 ESP.restart();
}
void SettingsApp::mediaP4UpdateCb(lv_event_t* e){auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;
 if(self->mediaInstallConfirm_!=1){self->mediaInstallConfirm_=1;self->rebuild();return;}
 self->mediaInstallConfirm_=0;FirmwareMediaUpdateService::instance().start(false);self->rebuild();}
void SettingsApp::mediaC6UpdateCb(lv_event_t* e){auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;
 if(self->mediaInstallConfirm_!=2){self->mediaInstallConfirm_=2;self->rebuild();return;}
 self->mediaInstallConfirm_=0;FirmwareMediaUpdateService::instance().start(true);self->rebuild();}
void SettingsApp::mediaRefreshCb(lv_event_t* e){auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(self)self->rebuild();}
void SettingsApp::c6UpdateCb(lv_event_t* e){auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;if(C6UpdateService::instance().requestOfficialUpdate())self->rebuild();}

void SettingsApp::categoryCb(lv_event_t* e){
    SecurityService::instance().lockSettings();
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*button=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!button)return;
    self->sdFormatConfirmation_.reset();
    const auto nextPage = static_cast<Page>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
    if (nextPage == self->page_ && !self->nasBrowse_ && !self->nasSetup_ && !self->wifiSetup_) return;
    self->page_ = nextPage;
    if (self->bleTimer_) { lv_timer_delete(self->bleTimer_); self->bleTimer_ = nullptr; }
    self->bleStatus_ = nullptr;
    if(self->nasTimer_){lv_timer_delete(self->nasTimer_);self->nasTimer_=nullptr;}
    self->nasBrowse_=false;
    self->rebuild();
}

void SettingsApp::sdFormatCb(lv_event_t* e){auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self||self->page_!=Page::Storage||!StorageService::instance().sdAvailable())return;
    if(!SecurityService::instance().settingsAllowed()){self->sdFormatConfirmation_.reset();SecurityUi::requestSettingsUnlock();return;}
    const uint32_t now=millis();if(!self->sdFormatConfirmation_.stage(now))self->sdFormatConfirmation_.begin(now);
    else if(self->sdFormatConfirmation_.confirm(now))StorageService::instance().requestSdFormat();
    self->rebuild();
}
void SettingsApp::sdFormatCancelCb(lv_event_t* e){auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(self){self->sdFormatConfirmation_.reset();self->rebuild();}}

void SettingsApp::audioVolumeChanged(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*button=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!button)return;
    auto s=SettingsService::instance().snapshot();
    const auto change=static_cast<intptr_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
    s.music.volume=static_cast<uint8_t>(constrain(static_cast<int>(s.music.volume)+static_cast<int>(change),0,100));
    SettingsService::instance().save(s);AudioHardwareService::instance().setVolume(s.music.volume);IdleService::instance().activity();self->rebuild();
}

void SettingsApp::volumeSliderChanged(lv_event_t* e){
    auto*slider=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!slider)return;
    const uint8_t value=static_cast<uint8_t>(lv_slider_get_value(slider));AudioHardwareService::instance().setVolume(value);
    if(lv_event_get_code(e)==LV_EVENT_RELEASED){auto s=SettingsService::instance().snapshot();s.music.volume=value;SettingsService::instance().save(s);IdleService::instance().activity();}
}

void SettingsApp::energyChanged(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self||!self->energySwitch_)return;
    auto s=SettingsService::instance().snapshot();s.powerManagement.energySaving=lv_obj_has_state(self->energySwitch_,LV_STATE_CHECKED);
    SettingsService::instance().save(s);PowerManager::instance().settingsChanged();IdleService::instance().activity();self->rebuild();
}

void SettingsApp::wakeTapChanged(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self||!self->wakeTapSwitch_)return;
    SettingsService::instance().setWakeOnSingleTap(lv_obj_has_state(self->wakeTapSwitch_,LV_STATE_CHECKED));IdleService::instance().activity();
}


void SettingsApp::idleProfileChanged(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self ||
        !self->idleActiveBrightness_ ||
        !self->idleDimAfter_ ||
        !self->idleDimBrightness_ ||
        !self->idleSaverAfter_ ||
        !self->idleSaverBrightness_ ||
        !self->idleOffAfter_ ||
        !self->alwaysOnSwitch_ ||
        !self->alwaysOnBrightness_) return;

    const int activePct = lv_slider_get_value(self->idleActiveBrightness_);
    const int dimAfter = lv_slider_get_value(self->idleDimAfter_);
    const int dimPct = lv_slider_get_value(self->idleDimBrightness_);
    int saverAfter = lv_slider_get_value(self->idleSaverAfter_);
    const int saverPct = lv_slider_get_value(self->idleSaverBrightness_);
    int offMinutes = lv_slider_get_value(self->idleOffAfter_);
    const int aodPct = lv_slider_get_value(self->alwaysOnBrightness_);

    saverAfter = max(saverAfter, dimAfter + 5);
    int offAfter = max(offMinutes * 60, saverAfter + 5);
    offMinutes = max(1, (offAfter + 59) / 60);

    setPercentLabel(self->idleActiveValue_, activePct);
    setSecondsLabel(self->idleDimAfterValue_, dimAfter);
    setPercentLabel(self->idleDimBrightnessValue_, dimPct);
    setSecondsLabel(self->idleSaverAfterValue_, saverAfter);
    setPercentLabel(self->idleSaverBrightnessValue_, saverPct);
    setMinutesLabel(self->idleOffAfterValue_, offMinutes);
    setPercentLabel(self->alwaysOnBrightnessValue_, aodPct);

    if (lv_event_get_code(e) != LV_EVENT_RELEASED &&
        lv_event_get_target(e) != self->alwaysOnSwitch_) return;

    auto s = SettingsService::instance().snapshot();
    s.brightness = percentToBrightness(activePct);
    s.normal.dimAfter = static_cast<uint16_t>(dimAfter);
    s.normal.dimBrightness = percentToBrightness(dimPct);
    s.normal.saverEnabled = true;
    s.normal.saverAfter = static_cast<uint16_t>(saverAfter);
    s.normal.saverBrightness = percentToBrightness(saverPct);
    s.normal.offAfter = static_cast<uint16_t>(min(offAfter, 65535));
    s.normal.alwaysOnEnabled = lv_obj_has_state(self->alwaysOnSwitch_, LV_STATE_CHECKED);
    s.normal.alwaysOnBrightness = percentToBrightness(aodPct);

    SettingsService::instance().save(s);
    if (DisplayManager::instance().brightness() != s.brightness)
        DisplayManager::instance().setBrightness(s.brightness);
    IdleService::instance().activity();
    self->rebuild();
}

void SettingsApp::wifiScheduleChanged(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self ||
        !self->wifiScheduleSwitch_ ||
        !self->wifiScheduleStartHour_ ||
        !self->wifiScheduleStartMinute_ ||
        !self->wifiScheduleEndHour_ ||
        !self->wifiScheduleEndMinute_) {
        return;
    }

    auto s = SettingsService::instance().snapshot();

    s.networkEco.wifiScheduleEnabled =
        lv_obj_has_state(self->wifiScheduleSwitch_, LV_STATE_CHECKED);

    const uint16_t startHour =
        static_cast<uint16_t>(lv_dropdown_get_selected(self->wifiScheduleStartHour_));
    const uint16_t startMinute =
        static_cast<uint16_t>(lv_dropdown_get_selected(self->wifiScheduleStartMinute_));
    const uint16_t endHour =
        static_cast<uint16_t>(lv_dropdown_get_selected(self->wifiScheduleEndHour_));
    const uint16_t endMinute =
        static_cast<uint16_t>(lv_dropdown_get_selected(self->wifiScheduleEndMinute_));

    s.networkEco.wifiScheduleStartMinute =
        static_cast<uint16_t>(startHour * 60 + startMinute);
    s.networkEco.wifiScheduleEndMinute =
        static_cast<uint16_t>(endHour * 60 + endMinute);

    SettingsService::instance().save(s);
    NetworkService::instance().settingsChanged();

    // Editing the schedule counts as activity, so WLAN is not switched off
    // underneath the user while this page is open.
    IdleService::instance().activity();
}

void SettingsApp::brightnessChanged(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*button=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!self||!button)return;
    const auto change=static_cast<intptr_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
    const int brightness=constrain(static_cast<int>(SettingsService::instance().snapshot().brightness)+static_cast<int>(change),15,255);
    const uint8_t value=static_cast<uint8_t>(brightness);
    if(SettingsService::instance().setBrightness(value)) DisplayManager::instance().setBrightness(value);
    IdleService::instance().activity();self->rebuild();
}

void SettingsApp::brightnessSliderChanged(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*slider=static_cast<lv_obj_t*>(lv_event_get_target(e));if(!self||!slider)return;
    const uint8_t value=static_cast<uint8_t>(lv_slider_get_value(slider));DisplayManager::instance().setBrightness(value);
    if(lv_event_get_code(e)==LV_EVENT_RELEASED){SettingsService::instance().setBrightness(value);IdleService::instance().activity();self->rebuild();}
}

void SettingsApp::wifiEcoChanged(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self) return;
    auto* changed = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!changed) return;

    const bool enabled = lv_obj_has_state(changed, LV_STATE_CHECKED);
    auto& settings = SettingsService::instance();
    bool saved = false;
    if (changed == self->wifiOffOnLanSwitch_) {
        saved = settings.setWifiOffWhenLan(enabled);
    } else if (changed == self->wifiPowerSaveSwitch_) {
        saved = settings.setWifiPowerSave(enabled);
    } else {
        return;
    }

    if (!saved) {
        self->rebuild();  // Restore the last saved switch state.
        return;
    }
    // Do not reload WiFi profiles, reconfigure addressing or interrupt C6:
    // the network loop reads these two policy flags from SettingsService.
    IdleService::instance().activity();
}

void SettingsApp::wifiSetupCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;
    self->wifiSetup_=!self->wifiSetup_;
    if(self->wifiSetup_)self->wifiProfileIndex_=NetworkService::instance().activeWifiProfile();
    self->rebuild();
}

void SettingsApp::wifiProfileCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*button=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!button)return;
    const uint8_t slot=static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
    if(slot>=NetworkService::kMaxWifiProfiles)return;
    self->wifiProfileIndex_=slot;
    self->wifiDeleteConfirmSlot_=-1;
    self->rebuild();
}

void SettingsApp::wifiOverviewPreferCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto* button=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!button)return;
    const uint8_t slot=static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
    auto& net=NetworkService::instance();
    if(slot>=NetworkService::kMaxWifiProfiles || !net.wifiProfileConfigured(slot))return;
    if(net.setPreferredWifiProfile(slot)){
        self->wifiProfileIndex_=slot;
        IdleService::instance().activity();
        self->rebuild();
    }
}

void SettingsApp::wifiPreferCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    if(NetworkService::instance().setPreferredWifiProfile(self->wifiProfileIndex_)){
        self->wifiDeleteConfirmSlot_=-1;
        IdleService::instance().activity();
        self->rebuild();
    } else if(self->wifiStatus_) {
        lv_label_set_text(self->wifiStatus_,"WLAN ist nicht gespeichert oder Prioritaet konnte nicht gespeichert werden.");
    }
}

void SettingsApp::wifiForgetCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    const int8_t slot=static_cast<int8_t>(self->wifiProfileIndex_);
    if(self->wifiDeleteConfirmSlot_ != slot ||
       (uint32_t)(millis()-self->wifiDeleteConfirmAt_) > 5000UL){
        self->wifiDeleteConfirmSlot_=slot;
        self->wifiDeleteConfirmAt_=millis();
        auto* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (button && lv_obj_get_child_count(button))
            lv_label_set_text(lv_obj_get_child(button, 0), "Erneut loeschen");
        if(self->wifiStatus_)
            lv_label_set_text(self->wifiStatus_,"Bestaetigen: Profil und Passwort innerhalb von 5 Sekunden erneut loeschen.");
        return;
    }
    self->wifiDeleteConfirmSlot_=-1;
    if(NetworkService::instance().forgetWifiProfile(static_cast<uint8_t>(slot))){
        IdleService::instance().activity();
        self->rebuild();
    } else if(self->wifiStatus_) {
        lv_label_set_text(self->wifiStatus_,"Loeschen fehlgeschlagen. NVS-Speicher oder Schreibschutz pruefen.");
    }
}

void SettingsApp::buildWifiSetup(){
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root_, 12, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto* top = lv_obj_create(root_);
    makeTransparent(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 46);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* back = lv_button_create(top);
    lv_obj_set_size(back, 150, 46);
    UiTheme::instance().stylePrimaryButton(back);
    lv_obj_add_event_cb(back, wifiSetupCb, LV_EVENT_CLICKED, this);
    auto* backLabel = lv_label_create(back);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Zurueck");
    makeTextReadable(backLabel);
    lv_obj_center(backLabel);

    auto* tabs = lv_obj_create(root_);
    makeTransparent(tabs);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, 44);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        tabs,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(tabs, 9, 0);

    for(uint8_t i=0;i<NetworkService::kMaxWifiProfiles;++i){
        auto* b = lv_button_create(tabs);
        lv_obj_set_size(b, 124, 36);
        styleSettingsNavButton(b, i==wifiProfileIndex_);
        lv_obj_add_event_cb(b,wifiProfileCb,LV_EVENT_CLICKED,this);
        lv_obj_set_user_data(
            b,
            reinterpret_cast<void*>(static_cast<uintptr_t>(i))
        );

        auto* l = lv_label_create(b);
        String name = "WLAN " + String(i+1);
        if(NetworkService::instance().wifiProfileConfigured(i) &&
           NetworkService::instance().preferredWifiProfile()==i)name += " (P)";
        lv_label_set_text(l,name.c_str());
        SettingsWidgets::readableText(l, &lv_font_montserrat_18);
        lv_obj_center(l);
    }

    auto* shareQr = lv_button_create(tabs);
    lv_obj_set_size(shareQr, 124, 36);
    styleSettingsNavButton(shareQr, false);
    lv_obj_add_event_cb(shareQr,wifiQrCb,LV_EVENT_CLICKED,this);
    auto* shareQrLabel=lv_label_create(shareQr);
    lv_label_set_text(shareQrLabel,"QR-Code");
    lv_obj_set_style_text_font(shareQrLabel,fontWithGermanFallback(&lv_font_montserrat_18),0);
    makeTextReadable(shareQrLabel);
    lv_obj_center(shareQrLabel);

    String savedSsid,savedPassword;
    WifiProfileAddressing addressing;
    NetworkService::instance().getWifiProfile(
        wifiProfileIndex_,
        savedSsid,
        savedPassword,
        addressing
    );

    auto* body = lv_obj_create(root_);
    makeTransparent(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 12, 0);

    auto* form = lv_obj_create(body);
    lv_obj_set_height(form, LV_PCT(100));
    lv_obj_set_flex_grow(form, 1);
    UiTheme::instance().styleGlassPanel(form);
    lv_obj_set_style_pad_all(form, 16, 0);
    lv_obj_set_style_pad_row(form, 10, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    configureScrollSurface(form);
    lv_obj_add_event_cb(form, wifiFormScrollCb, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(form, wifiFormScrollCb, LV_EVENT_SCROLL, this);

    auto* profileTitle = lv_label_create(form);
    String profileText =
        "Profil " + String(wifiProfileIndex_+1) +
        (wifiProfileIndex_==NetworkService::instance().activeWifiProfile()
            ? " (zuletzt verbunden)"
            : "") +
        (wifiProfileIndex_==NetworkService::instance().preferredWifiProfile() &&
         NetworkService::instance().wifiProfileConfigured(wifiProfileIndex_)
            ? " (bevorzugt)" : "");
    lv_label_set_text(profileTitle,profileText.c_str());
    lv_obj_set_style_text_font(
        profileTitle,
        fontWithGermanFallback(&lv_font_montserrat_18),
        0
    );

    auto field=[&](
        const char* label,
        lv_obj_t*& target,
        const String& value,
        bool secret=false
    ){
        auto* l=lv_label_create(form);
        lv_label_set_text(l,label);
        lv_obj_set_width(l,LV_PCT(100));

        target=lv_textarea_create(form);
        styleSettingsInput(target);
        lv_obj_set_width(target,LV_PCT(100));
        lv_obj_set_height(target,44);
        lv_textarea_set_one_line(target,true);
        lv_textarea_set_max_length(target,secret?64:48);
        lv_textarea_set_text(target,value.c_str());
        if(secret)lv_textarea_set_password_mode(target,true);
        lv_obj_add_event_cb(
            target,
            textareaFocusCb,
            LV_EVENT_PRESSED,
            this
        );
        lv_obj_add_event_cb(
            target,
            textareaFocusCb,
            LV_EVENT_SHORT_CLICKED,
            this
        );
    };

    field("SSID",wifiSsid_,savedSsid);
    field("Passwort",wifiPassword_,savedPassword,true);

    auto* passRow = makeSettingsRow(form, 48);
    makeFieldLabel(passRow, "Passwort anzeigen", 180);
    wifiPasswordVisibility_=lv_switch_create(passRow);
    lv_obj_add_event_cb(
        wifiPasswordVisibility_,
        wifiPasswordVisibilityCb,
        LV_EVENT_VALUE_CHANGED,
        this
    );

    field("IP-Adresse (leer = DHCP)",wifiIp_,addressing.ip);
    field("Gateway",wifiGateway_,addressing.gateway);
    field(
        "Subnetz",
        wifiSubnet_,
        addressing.subnet.length()
            ? addressing.subnet
            : String("255.255.255.0")
    );
    field("DNS 1",wifiDns1_,addressing.dns1);
    field("DNS 2",wifiDns2_,addressing.dns2);

    auto* buttons = makeSettingsRow(form, 58);
    auto* connect=lv_button_create(buttons);
    lv_obj_set_size(connect,250,48);
    UiTheme::instance().stylePrimaryButton(connect);
    lv_obj_add_event_cb(connect,wifiConnectCb,LV_EVENT_CLICKED,this);
    auto* connectLabel=lv_label_create(connect);
    lv_label_set_text(connectLabel,"Speichern + verbinden");
    makeTextReadable(connectLabel);
    lv_obj_center(connectLabel);

    // Per-profile controls are never shown for an empty slot.
    if(NetworkService::instance().wifiProfileConfigured(wifiProfileIndex_)){
        auto* manageRow=makeSettingsRow(form, 58);
        auto* prefer=lv_button_create(manageRow);
        lv_obj_set_size(prefer,255,46);
        UiTheme::instance().stylePrimaryButton(prefer);
        const bool preferred=NetworkService::instance().preferredWifiProfile()==wifiProfileIndex_;
        if(!preferred)lv_obj_add_event_cb(prefer,wifiPreferCb,LV_EVENT_CLICKED,this);
        else lv_obj_add_state(prefer,LV_STATE_DISABLED);
        auto* preferText=lv_label_create(prefer);
        lv_label_set_text(preferText,preferred?"Bevorzugtes WLAN":"Als bevorzugt festlegen");
        makeTextReadable(preferText);
        lv_obj_center(preferText);

        auto* forget=lv_button_create(manageRow);
        lv_obj_set_size(forget,185,46);
        lv_obj_set_style_bg_color(forget,lv_color_hex(0x842A3B),0);
        lv_obj_set_style_text_color(forget,lv_color_hex(0xFFFFFF),0);
        lv_obj_add_event_cb(forget,wifiForgetCb,LV_EVENT_CLICKED,this);
        auto* forgetText=lv_label_create(forget);
        lv_label_set_text(forgetText,"Profil loeschen");
        makeTextReadable(forgetText);
        lv_obj_center(forgetText);
    }

    auto* enabledLabel=lv_label_create(buttons);
    lv_label_set_text(enabledLabel,"WLAN aktiv");
    auto* enabled=lv_switch_create(buttons);
    if(NetworkService::instance().wifiEnabled())
        lv_obj_add_state(enabled,LV_STATE_CHECKED);
    lv_obj_add_event_cb(enabled,wifiEnabledCb,LV_EVENT_VALUE_CHANGED,this);

    auto* list = lv_obj_create(body);
    lv_obj_set_width(list, 430);
    lv_obj_set_height(list, LV_PCT(100));
    UiTheme::instance().styleGlassPanel(list);
    configureScrollSurface(list);
    lv_obj_set_style_pad_all(list, 14, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    const auto wifiState=NetworkService::instance().wifiConnectionState();

    wifiStatus_=lv_label_create(list);
    lv_obj_set_width(wifiStatus_,LV_PCT(100));
    lv_label_set_long_mode(wifiStatus_, LV_LABEL_LONG_WRAP);

    String status=
        "C6: "+String(wifiState.c6Ready?"bereit":"nicht bereit")+
        "\nStatus: "+wifiState.text;
    if(wifiState.connected){
        status+="\nIP: "+wifiState.ip+
                "\nDNS: "+wifiState.dns1;
    }
    lv_label_set_text(wifiStatus_,status.c_str());

    auto* scan=lv_button_create(list);
    lv_obj_set_width(scan,LV_PCT(100));
    lv_obj_set_height(scan,50);
    UiTheme::instance().stylePrimaryButton(scan);

    if(board::kC6HostedScanQualified)
        lv_obj_add_event_cb(scan,wifiScanCb,LV_EVENT_CLICKED,this);
    else
        lv_obj_add_state(scan,LV_STATE_DISABLED);

    auto* scanLabel=lv_label_create(scan);
    lv_label_set_text(
        scanLabel,
        board::kC6HostedScanQualified
            ? LV_SYMBOL_REFRESH " Netzwerke suchen"
            : "WLAN-Suche nicht verfuegbar"
    );
    lv_obj_center(scanLabel);

    wifiNetworksList_ = lv_obj_create(list);
    makeTransparent(wifiNetworksList_);
    lv_obj_set_width(wifiNetworksList_, LV_PCT(100));
    lv_obj_set_height(wifiNetworksList_, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifiNetworksList_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(wifiNetworksList_, 0, 0);
    lv_obj_set_style_pad_row(wifiNetworksList_, 5, 0);
    renderWifiNetworkList();

    createKeyboard();

    for(size_t i=0;i<savedPassword.length();++i)
        savedPassword.setCharAt(i,'\0');
}

void SettingsApp::bleEnabledCb(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self || !self->bleEnabledSwitch_) return;
    const bool wanted = lv_obj_has_state(self->bleEnabledSwitch_, LV_STATE_CHECKED);
    auto& ble = BluetoothService::instance();
    const bool ok = ble.setEnabled(wanted);
    if (!ok && self->bleStatus_) {
        lv_label_set_text(self->bleStatus_,
            ble.scanning() ? "Bitte laufende BLE-Suche abwarten und erneut schalten."
                           : "Bluetooth konnte nicht umgeschaltet werden.");
    }
    // Reflect actual state if disabling during scan or enabling failed.
    if (ble.enabled()) lv_obj_add_state(self->bleEnabledSwitch_, LV_STATE_CHECKED);
    else lv_obj_clear_state(self->bleEnabledSwitch_, LV_STATE_CHECKED);
    // Queue refresh on LVGL's main event loop; the old UI remains valid until
    // the callback returns. No modification to ESP32-C6 firmware.
    lv_async_call([](void* ctx) {
        auto* app = static_cast<SettingsApp*>(ctx);
        if (app && app->root_ && app->page_ == Page::Bluetooth) app->rebuild();
    }, self);
}

void SettingsApp::bleScanCb(lv_event_t* e) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if (!self) return;

    auto& ble = BluetoothService::instance();
    if (!ble.available()) {
        if (self->bleStatus_)
            lv_label_set_text(self->bleStatus_, ble.status().c_str());
        return;
    }

    if (!ble.startScan(8)) {
        if (self->bleStatus_)
            lv_label_set_text(self->bleStatus_, ble.status().c_str());
        return;
    }

    if (self->bleStatus_)
        lv_label_set_text(self->bleStatus_, "BLE-Suche laeuft ...");

    if (self->bleTimer_) {
        lv_timer_delete(self->bleTimer_);
        self->bleTimer_ = nullptr;
    }
    self->bleTimer_ = lv_timer_create(blePollCb, 500, self);
}

void SettingsApp::blePollCb(lv_timer_t* timer) {
    auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
    if (!self) return;

    auto& ble = BluetoothService::instance();

    if (self->bleStatus_) {
        String text =
            String("Status: ") + ble.status() +
            "\nGefundene Geraete: " + String(ble.deviceCount());
        if (text != lv_label_get_text(self->bleStatus_))
            lv_label_set_text(self->bleStatus_, text.c_str());
    }

    if (ble.scanning()) return;

    if (self->bleTimer_) {
        lv_timer_delete(self->bleTimer_);
        self->bleTimer_ = nullptr;
    }

    if (self->root_ && self->page_ == Page::Bluetooth) {
        if (self->scrollBusy()) self->requestDeferredRefresh(false,true,false);
        else self->rebuild();
    }
}


void SettingsApp::nasOpenCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    auto* btn=static_cast<lv_obj_t*>(lv_event_get_target(e));
    size_t idx=static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn)));
    auto& nas=NasService::instance();
    if(idx>=nas.profileCount())return;
    self->nasSelectedProfile_=static_cast<uint8_t>(idx);
    self->nasCurrentFolder_="";
    self->nasBrowse_=true;
    nas.startSmbBrowse(idx,self->nasCurrentFolder_);
    self->nasSeenGeneration_=nas.browseState().generation;
    self->rebuild();
}

void SettingsApp::nasBackCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self||NasService::instance().browseState().busy)return;
    if(self->nasCurrentFolder_.length()){
        int slash=self->nasCurrentFolder_.lastIndexOf('/');
        self->nasCurrentFolder_=slash<0?String():self->nasCurrentFolder_.substring(0,slash);
        NasService::instance().startSmbBrowse(self->nasSelectedProfile_,self->nasCurrentFolder_);
        self->nasSeenGeneration_=NasService::instance().browseState().generation;
    }else{
        self->nasBrowse_=false;
        if(self->nasTimer_){lv_timer_delete(self->nasTimer_);self->nasTimer_=nullptr;}
    }
    self->rebuild();
}

void SettingsApp::nasFileCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    auto* btn=static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint8_t index=static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn)));
    NasBrowseEntry entry{};
    auto& nas=NasService::instance();
    if(!nas.browseEntry(index,entry))return;
    if(entry.directory){
        self->nasCurrentFolder_=entry.path;
        nas.startSmbBrowse(self->nasSelectedProfile_,self->nasCurrentFolder_);
    }else{
        nas.startSmbDownload(index);
    }
    self->nasSeenGeneration_=nas.browseState().generation;
    self->rebuild();
}

void SettingsApp::nasPollCb(lv_timer_t* t){
    auto* self=static_cast<SettingsApp*>(lv_timer_get_user_data(t));
    if(!self||!self->root_||!self->nasBrowse_)return;
    const auto state=NasService::instance().browseState();
    if(!state.busy && state.generation!=self->nasSeenGeneration_){
        self->nasSeenGeneration_=state.generation;
        if(self->scrollBusy()) self->requestDeferredRefresh(false,false,true);
        else self->rebuild();
    }
}

void SettingsApp::buildNasBrowser(){
    lv_obj_set_flex_flow(root_,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root_,12,0);
    lv_obj_set_style_pad_row(root_,10,0);
    auto* header=makeSettingsRow(root_,65);
    auto* back=lv_button_create(header);
    lv_obj_set_size(back,200,52);
    UiTheme::instance().stylePrimaryButton(back);
    lv_obj_add_event_cb(back,nasBackCb,LV_EVENT_CLICKED,this);
    auto* backLabel=lv_label_create(back);
    lv_label_set_text(backLabel,LV_SYMBOL_LEFT " Zurueck");
    makeTextReadable(backLabel);lv_obj_center(backLabel);
    auto* folder=lv_label_create(header);
    String folderText="SMB: /"+nasCurrentFolder_;
    lv_label_set_text(folder,folderText.c_str());
    lv_obj_set_width(folder,650);
    makeTextReadable(folder);
    auto* panel=lv_obj_create(root_);
    UiTheme::instance().styleGlassPanel(panel);
    lv_obj_set_width(panel,LV_PCT(100));
    lv_obj_set_flex_grow(panel,1);
    lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel,LV_DIR_VER);
    configureScrollSurface(panel);
    lv_obj_set_style_pad_row(panel,10,0);
    auto state=NasService::instance().browseState();
    auto* status=lv_label_create(panel);
    lv_label_set_text(status,state.message);
    lv_obj_set_width(status,LV_PCT(100));
    lv_label_set_long_mode(status,LV_LABEL_LONG_WRAP);
    makeTextReadable(status);
    if(!state.busy && state.success){
        for(uint8_t i=0;i<state.count;++i){
            NasBrowseEntry entry{};
            if(!NasService::instance().browseEntry(i,entry))continue;
            auto* b=lv_button_create(panel);
            lv_obj_set_size(b,LV_PCT(100),54);
            UiTheme::instance().styleGlassPanel(b);
            lv_obj_set_style_shadow_width(b,0,0);
            lv_obj_set_user_data(b,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
            lv_obj_add_event_cb(b,nasFileCb,LV_EVENT_CLICKED,this);
            auto* l=lv_label_create(b);
            String label=String(entry.directory?"[ORDNER]  ":"[DATEI]  ")+String(entry.name);
            if(!entry.directory)label+="  ("+String(static_cast<unsigned long>(entry.size/1024))+" KiB) - auf SD kopieren";
            lv_label_set_text(l,label.c_str());
            lv_obj_set_width(l,LV_PCT(96));
            lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
            makeTextReadable(l);lv_obj_center(l);
        }
    }
    if(!nasTimer_)nasTimer_=lv_timer_create(nasPollCb,500,this);
}

void SettingsApp::nasSetupCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;self->nasSetup_=!self->nasSetup_;self->rebuild();
}

void SettingsApp::buildNasSetup(){
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root_, 12, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto* top=lv_obj_create(root_);
    makeTransparent(top);
    lv_obj_set_width(top,LV_PCT(100));
    lv_obj_set_height(top,56);
    lv_obj_set_flex_flow(top,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        top,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    auto* title=lv_label_create(top);
    lv_label_set_text(title,"SMB / WebDAV einrichten");
    lv_obj_set_style_text_font(
        title,
        fontWithGermanFallback(&lv_font_montserrat_28),
        0
    );

    auto* back=lv_button_create(top);
    lv_obj_set_size(back,150,46);
    UiTheme::instance().stylePrimaryButton(back);
    lv_obj_add_event_cb(back,nasSetupCb,LV_EVENT_CLICKED,this);

    auto* backLabel=lv_label_create(back);
    lv_label_set_text(backLabel,LV_SYMBOL_LEFT " Zurueck");
    lv_obj_center(backLabel);

    auto* form=lv_obj_create(root_);
    lv_obj_set_width(form,LV_PCT(100));
    lv_obj_set_flex_grow(form,1);
    UiTheme::instance().styleGlassPanel(form);
    lv_obj_set_style_pad_all(form,18,0);
    lv_obj_set_style_pad_row(form,10,0);
    lv_obj_set_flex_flow(form,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(form,LV_DIR_VER);
    configureScrollSurface(form);

    auto field=[&](
        const char* label,
        lv_obj_t*& target,
        const char* value="",
        bool secret=false
    ){
        auto* l=lv_label_create(form);
        lv_label_set_text(l,label);
        lv_obj_set_width(l,LV_PCT(100));

        target=lv_textarea_create(form);
        styleSettingsInput(target);
        lv_obj_set_width(target,LV_PCT(100));
        lv_obj_set_height(target,44);
        lv_textarea_set_one_line(target,true);
        lv_textarea_set_max_length(target,secret?192:127);
        lv_textarea_set_text(target,value);
        if(secret)lv_textarea_set_password_mode(target,true);
        lv_obj_add_event_cb(
            target,
            textareaFocusCb,
            LV_EVENT_PRESSED,
            this
        );
        lv_obj_add_event_cb(
            target,
            textareaFocusCb,
            LV_EVENT_CLICKED,
            this
        );
    };

    field("Profilname",nasName_,"Mein NAS");
    auto* protocolLabel=lv_label_create(form);
    lv_label_set_text(protocolLabel,"Protokoll");
    lv_obj_set_style_text_color(protocolLabel,UiTheme::instance().textPrimary(),0);
    nasProtocol_=lv_dropdown_create(form);
    lv_dropdown_set_options(nasProtocol_,"SMB2/3\nWebDAV");
    lv_obj_set_width(nasProtocol_,360);
    styleSettingsInput(nasProtocol_);
    field("Server / IP-Adresse (z.B. fritz.box oder Unraid-IP)",nasHost_);
    field("Port (SMB: 445)",nasPort_,"445");
    field("SMB: Freigabe[/Unterordner] | WebDAV: /pfad",nasPath_,"Musik");
    field("Benutzername",nasUsername_);
    field("Passwort",nasPassword_,"",true);

    auto* action=makeSettingsRow(form,64);

    auto* save=lv_button_create(action);
    lv_obj_set_size(save,280,50);
    UiTheme::instance().stylePrimaryButton(save);
    lv_obj_add_event_cb(save,nasSaveCb,LV_EVENT_CLICKED,this);

    auto* saveLabel=lv_label_create(save);
    lv_label_set_text(saveLabel,"Sicher speichern");
    makeTextReadable(saveLabel);
    lv_obj_center(saveLabel);

    nasStatus_=lv_label_create(form);
    lv_obj_set_width(nasStatus_,LV_PCT(100));
    lv_label_set_long_mode(nasStatus_,LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        nasStatus_,
        "SMB: Freigabename in das Pfadfeld eintragen (z.B. Musik, "
        "FRITZ.NAS oder Musik/Alben). SMB nur Port 445. "
        "Unraid/FRITZ!Box Freigabe und Benutzerrechte vorher einrichten. "
        "WebDAV: Protokoll umstellen, Port/Pfad entsprechend anpassen."
    );

    createKeyboard();
}

void SettingsApp::nasSaveCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self||!self->nasProtocol_||!self->nasName_||!self->nasHost_||!self->nasPort_||!self->nasPath_||!self->nasUsername_||!self->nasPassword_||!self->nasStatus_)return;
    const long port=strtol(lv_textarea_get_text(self->nasPort_),nullptr,10);
    if(port<1||port>65535){lv_label_set_text(self->nasStatus_,"Port muss zwischen 1 und 65535 liegen.");return;}
    const bool useSmb = lv_dropdown_get_selected(self->nasProtocol_)==0;
    if(useSmb && port!=445){
        lv_label_set_text(self->nasStatus_,"SMB nutzt hier ausschliesslich Port 445.");return;
    }
    String path=lv_textarea_get_text(self->nasPath_);
    path.trim();
    if(useSmb && (path.length()==0 || path=="/" || path.indexOf("..")>=0 ||
                  path.indexOf('\\')>=0 || path.indexOf(':')>=0)){
        lv_label_set_text(self->nasStatus_,"SMB-Pfad: Freigabename eingeben, z.B. Musik.");return;
    }
    NasProfile profile{};profile.protocol=useSmb?NasProtocol::Smb2:NasProtocol::WebDav;
    profile.enabled=true;profile.port=static_cast<uint16_t>(port);
    strlcpy(profile.name,lv_textarea_get_text(self->nasName_),sizeof(profile.name));strlcpy(profile.host,lv_textarea_get_text(self->nasHost_),sizeof(profile.host));strlcpy(profile.path,lv_textarea_get_text(self->nasPath_),sizeof(profile.path));strlcpy(profile.username,lv_textarea_get_text(self->nasUsername_),sizeof(profile.username));
    if(NasService::instance().addProfile(profile,lv_textarea_get_text(self->nasPassword_))){lv_label_set_text(self->nasStatus_,useSmb?
        "SMB-Profil gespeichert. Zur NAS-Uebersicht zurueck und Profil oeffnen.":
        "WebDAV-Profil gespeichert. Zugangsdaten bleiben geschuetzt.");IdleService::instance().activity();}else lv_label_set_text(self->nasStatus_,"Profil konnte nicht gespeichert werden. Name, Server und Speicher pruefen.");
    self->hideKeyboard();
}


// A single keyboard per open setup page, reused for every text field.
// Keep it out of the root flex layout: it is an overlay, not another column.
void SettingsApp::createKeyboard() {
    if (!root_ || keyboard_) return;
    keyboard_ = lv_keyboard_create(root_);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(keyboard_, LV_PCT(100), 304);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(0x101D2E), 0);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(keyboard_, lv_color_hex(0x37536B), 0);
    lv_obj_set_style_border_width(keyboard_, 1, 0);
    lv_obj_set_style_radius(keyboard_, 18, 0);
    lv_obj_set_style_pad_all(keyboard_, 9, 0);
    lv_obj_set_style_pad_row(keyboard_, 6, 0);
    lv_obj_set_style_pad_column(keyboard_, 5, 0);
    // LVGL keyboard keys belong to the button-matrix ITEMS part.
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(0x243B56), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_, lv_color_hex(0xF5F7FA), LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard_, fontWithGermanFallback(&lv_font_montserrat_18), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard_, 10, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard_, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(0x42688C), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard_, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_keyboard_set_textarea(keyboard_, nullptr);
    lv_obj_add_event_cb(keyboard_, keyboardEventCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboardEventCb, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    keyboardNumeric_ = false;
}

void SettingsApp::showKeyboard(lv_obj_t* target) {
    if (!keyboard_ || !target) return;
    // Host names and passwords remain text, even when their current value is numeric.
    if(target==nasPort_ && nasSetup_)InputKeyboard::setMode(target,keyboardInput::Mode::Integer);
    // Avoid redundant LVGL redraws when focus and click fire for the same field.
    if (lv_keyboard_get_textarea(keyboard_) != target)
        InputKeyboard::bind(keyboard_, target);
    if (lv_obj_has_flag(keyboard_, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard_);
    // Only called after an intentional tap, never while a scroll is active.
    lv_obj_scroll_to_view(target, LV_ANIM_OFF);
}

void SettingsApp::hideKeyboard() {
    if (!keyboard_ || lv_obj_has_flag(keyboard_, LV_OBJ_FLAG_HIDDEN)) return;
    lv_keyboard_set_textarea(keyboard_, nullptr);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
}

void SettingsApp::textareaFocusCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    auto* target=static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* indev=lv_indev_active();
    const auto code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED){
        self->keyboardPressTarget_=target;
        self->keyboardPressValid_=indev!=nullptr;
        if(indev)lv_indev_get_point(indev,&self->keyboardPressPoint_);
        return;
    }
    if(code!=LV_EVENT_SHORT_CLICKED || !self->keyboardPressValid_ ||
       self->keyboardPressTarget_!=target || !indev)return;
    lv_point_t released{};
    lv_indev_get_point(indev,&released);
    const int dx=static_cast<int>(released.x)-self->keyboardPressPoint_.x;
    const int dy=static_cast<int>(released.y)-self->keyboardPressPoint_.y;
    self->keyboardPressValid_=false;
    // A scroll must never be interpreted as a click on the field.
    if((int32_t)(self->wifiScrollUntil_-millis())>0 || dx*dx+dy*dy>100)return;
    self->showKeyboard(target);
}

void SettingsApp::keyboardEventCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self||!self->keyboard_)return;
    auto* ta=lv_keyboard_get_textarea(self->keyboard_);
    if(ta)lv_obj_remove_state(ta,LV_STATE_FOCUSED);
    self->hideKeyboard();
}

void SettingsApp::wifiScanCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self)return;
    if(!NetworkService::instance().requestWifiScan()){
        if(self->scanTimer_){lv_timer_delete(self->scanTimer_);self->scanTimer_=nullptr;}
        if(self->wifiStatus_){const String status=NetworkService::instance().wifiScanStatus();lv_label_set_text(self->wifiStatus_,status.c_str());}
        return;
    }
    self->scanStartedAt_=millis();
    if(self->wifiStatus_)lv_label_set_text(self->wifiStatus_,"Suche nach WLAN-Netzen ...");
    if(!self->scanTimer_)self->scanTimer_=lv_timer_create(wifiScanPollCb,500,self);
}

void SettingsApp::wifiScanPollCb(lv_timer_t* timer){
    auto*self=static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
    if(!self||!self->wifiSetup_){lv_timer_pause(timer);if(self)lv_async_call(wifiScanRefreshCb,self);return;}
    if(millis()-self->scanStartedAt_>10000UL){
        lv_timer_pause(timer);
        if(self->wifiStatus_)lv_label_set_text(self->wifiStatus_,"WLAN-Suche hat nicht geantwortet. SSID manuell eingeben.");
        lv_async_call(wifiScanRefreshCb,self);return;
    }
    if(NetworkService::instance().wifiScanInProgress())return;
    const int count=NetworkService::instance().scanNetworks(self->scanResults_,12);
    self->scanCount_=count>0?static_cast<uint8_t>(count):0;
    if(self->wifiStatus_){const String status=NetworkService::instance().wifiScanStatus();lv_label_set_text(self->wifiStatus_,status.c_str());}
    lv_timer_pause(timer);lv_async_call(wifiScanRefreshCb,self);
}

void SettingsApp::wifiFormScrollCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self)return;
    self->wifiScrollUntil_=millis()+180;
    self->scrollBusyUntil_=millis()+140;
    self->keyboardPressValid_=false;
}

void SettingsApp::renderWifiNetworkList(){
    if(!wifiNetworksList_)return;
    lv_obj_clean(wifiNetworksList_);
    for(uint8_t i=0;i<scanCount_;++i){
        auto* row=lv_button_create(wifiNetworksList_);
        lv_obj_set_width(row,LV_PCT(100));
        lv_obj_set_height(row,46);
        lv_obj_set_style_bg_color(row,lv_color_hex(0x1A304A),0);
        lv_obj_set_style_text_color(row,lv_color_hex(0xF5F7FA),0);
        lv_obj_add_event_cb(row,wifiNetworkCb,LV_EVENT_SHORT_CLICKED,this);
        lv_obj_set_user_data(row,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        auto* label=lv_label_create(row);
        String text=scanResults_[i].ssid+"  "+String(scanResults_[i].rssi)+" dBm";
        lv_label_set_text(label,text.c_str());
        lv_obj_set_width(label,LV_PCT(100));
        lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(label,lv_color_hex(0xF5F7FA),0);
        lv_obj_center(label);
    }
}

void SettingsApp::wifiScanRefreshCb(void* context){
    auto*self=static_cast<SettingsApp*>(context);if(!self)return;
    if(self->scanTimer_){lv_timer_delete(self->scanTimer_);self->scanTimer_=nullptr;}
    // Never mutate a long list while the finger is moving. This avoids layout
    // recalculation and redraws in the middle of a scroll gesture.
    if(self->root_&&self->wifiSetup_){
        if(self->scrollBusy()) self->requestDeferredRefresh(true,false,false);
        else self->renderWifiNetworkList();
    }
}

void SettingsApp::wifiNetworkCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*row=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!row||!self->wifiSsid_)return;
    const size_t index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    if(index>=self->scanCount_)return;
    lv_textarea_set_text(self->wifiSsid_,self->scanResults_[index].ssid.c_str());
    lv_textarea_set_text(self->wifiPassword_,"");
    self->hideKeyboard();
    // Deliberate tap on the password field opens the keyboard.
}

void SettingsApp::wifiConnectCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self||!self->wifiSsid_||!self->wifiPassword_||!self->wifiStatus_)return;

    WifiProfileAddressing addressing;
    addressing.ip=self->wifiIp_?lv_textarea_get_text(self->wifiIp_):"";
    addressing.gateway=self->wifiGateway_?lv_textarea_get_text(self->wifiGateway_):"";
    addressing.subnet=self->wifiSubnet_?lv_textarea_get_text(self->wifiSubnet_):"";
    addressing.dns1=self->wifiDns1_?lv_textarea_get_text(self->wifiDns1_):"";
    addressing.dns2=self->wifiDns2_?lv_textarea_get_text(self->wifiDns2_):"";

    // DHCP: only IP must stay empty. Static mode requires IP, gateway and subnet.
    if(addressing.ip.length() && (!addressing.gateway.length() || !addressing.subnet.length())){
        lv_label_set_text(self->wifiStatus_,"Statische IP: Gateway und Subnetz muessen ausgefuellt sein.");
        return;
    }

    const bool saved=NetworkService::instance().connectWifiProfile(
        self->wifiProfileIndex_,
        lv_textarea_get_text(self->wifiSsid_),
        lv_textarea_get_text(self->wifiPassword_),
        addressing
    );
    const auto state=NetworkService::instance().wifiConnectionState();
    lv_label_set_text(self->wifiStatus_,saved?"Profil gespeichert. Verbindung wird hergestellt.":state.text.c_str());

    self->hideKeyboard();
}

void SettingsApp::wifiEnabledCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    auto*sw=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if(!self||!sw)return;
    NetworkService::instance().setWifiEnabled(lv_obj_has_state(sw,LV_STATE_CHECKED));IdleService::instance().activity();
}

void SettingsApp::wifiPasswordVisibilityCb(lv_event_t* e){
    auto*self=static_cast<SettingsApp*>(lv_event_get_user_data(e));
    if(!self||!self->wifiPasswordVisibility_||!self->wifiPassword_)return;
    const bool visible=lv_obj_has_state(self->wifiPasswordVisibility_,LV_STATE_CHECKED);
    lv_textarea_set_password_mode(self->wifiPassword_,!visible);
}

void SettingsApp::wifiQrCb(lv_event_t* e){
    auto* self=static_cast<SettingsApp*>(lv_event_get_user_data(e));if(!self||!self->root_)return;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){SecurityUi::requestSettingsUnlock();return;}
    String ssid,password;
    if(NetworkService::instance().getWifiProfile(self->wifiProfileIndex_,ssid,password)){
        String payload=QrGenerator::wifi(ssid,password);QrDialog::show(ssid.c_str(),payload,true,self->root_);
        if(payload.length())mbedtls_platform_zeroize(const_cast<char*>(payload.c_str()),payload.length());
    }
    if(password.length())mbedtls_platform_zeroize(const_cast<char*>(password.c_str()),password.length());
}

}
