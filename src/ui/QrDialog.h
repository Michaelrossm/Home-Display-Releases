#pragma once
#include <Arduino.h>
#include <lvgl.h>
namespace hp {
class QrDialog {public: static void show(const char* title,const String& payload,bool sensitive=false,lv_obj_t* owner=nullptr);};
}
