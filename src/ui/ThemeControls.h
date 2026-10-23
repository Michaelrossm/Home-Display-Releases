#pragma once
#include <lvgl.h>

namespace hp {

class ThemeControls {
public:
    static ThemeControls& instance();
    void buildControlCenter(lv_obj_t* parent);
    void refreshControlCenter();
    static void bindSelection(lv_obj_t* dropdown, bool accent);

private:
    ThemeControls() = default;
    static void changed(lv_event_t* e);
    static void syncSelection(lv_obj_t* dropdown, bool accent);
    lv_obj_t* mode_ = nullptr;
    lv_obj_t* accent_ = nullptr;
};

}
