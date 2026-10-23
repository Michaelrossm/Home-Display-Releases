#pragma once
#include <lvgl.h>
namespace hp {
// Dropdown backed by an existing hidden model field; no extra saved country state.
lv_obj_t* countryPicker(lv_obj_t* parent,lv_obj_t* model);
}
