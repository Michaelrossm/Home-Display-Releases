#pragma once
#include <lvgl.h>
namespace hp {
class SecurityUi {
public:
    static void buildPage(lv_obj_t* parent);
    static void sync(); // Existing UiShell pass with DisplayUiGuard held.
    static void requestSettingsUnlock();
};
}
