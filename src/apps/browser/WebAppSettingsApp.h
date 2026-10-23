#pragma once
#include "../../core/AppManager.h"

namespace hp {

// Historical filename/class retained so existing includes keep working.
// In the UI this is the central app-management screen, not a Web-App-only screen.
class WebAppSettingsApp : public IApp {
public:
    const char* id() const override { return "webapps"; }
    const char* name() const override { return "Apps"; }
    const char* icon() const override { return LV_SYMBOL_PLUS; }

    bool begin() override { return true; }
    void open(lv_obj_t* parent) override;
    void close() override { root_ = nullptr; }

private:
    static void addCb(lv_event_t* e);
    static void deleteCb(lv_event_t* e);
    static void nativeToggleCb(lv_event_t* e);
    static void luaSettingsCb(lv_event_t* e);
    void rebuild();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* name_ = nullptr;
    lv_obj_t* url_ = nullptr;
    lv_obj_t* status_ = nullptr;
};

}
