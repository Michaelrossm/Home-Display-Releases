#pragma once
#include <lvgl.h>
#include "InputMode.h"

namespace hp::InputKeyboard {
// Store only a static accepted-character string in LVGL, never app-owned data.
void setMode(lv_obj_t* field, keyboardInput::Mode mode);
void bind(lv_obj_t* keyboard, lv_obj_t* field);
// One reusable editor strip per keyboard; no app-specific mirrors/tasks.
bool prepare(lv_obj_t* keyboard);
void hide(lv_obj_t* keyboard);
}
