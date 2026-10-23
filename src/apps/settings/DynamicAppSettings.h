#pragma once
#include <lvgl.h>

namespace hp {

class DynamicAppSettings {
public:
    static DynamicAppSettings& instance();
    void update();
    void invalidate() { dirty_ = true; nextScanMs_=0; }

private:
    static void openCb(lv_event_t* e);
    static void enabledCb(lv_event_t* e);
    static void listDeletedCb(lv_event_t* e);
    lv_obj_t* findAppsPanel(lv_obj_t* root) const;
    lv_obj_t* findLabelRecursive(lv_obj_t* root, const char* text) const;
    void inject(lv_obj_t* panel);

    lv_obj_t* injectedPanel_ = nullptr;
    lv_obj_t* list_ = nullptr;
    bool dirty_ = false;
    uint32_t nextScanMs_=0;
};

}
