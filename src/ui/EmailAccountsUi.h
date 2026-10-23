#pragma once
#include <lvgl.h>
namespace hp {
class EmailAccountsUi {
public:
    static void build(lv_obj_t* parent, lv_event_cb_t focusCallback, void* focusContext);
};
}
