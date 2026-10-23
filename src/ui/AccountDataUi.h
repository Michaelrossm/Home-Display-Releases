#pragma once
#include <lvgl.h>
#include <Arduino.h>
namespace hp {
class AccountDataUi {public:static void build(lv_obj_t* parent,const String& accountId,lv_event_cb_t focus,void* context);};
}
