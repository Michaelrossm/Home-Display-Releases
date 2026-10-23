#include "InputKeyboard.h"
#include "UiTheme.h"
#include <new>
#include <cstring>

namespace hp::InputKeyboard {
namespace {
lv_obj_t* activeKeyboard=nullptr;
struct Editor {
    lv_obj_t* keyboard=nullptr;lv_obj_t* field=nullptr;lv_obj_t* text=nullptr;
    lv_obj_t* eye=nullptr;lv_obj_t* eyeLabel=nullptr;bool syncing=false;
};
void editorDeleted(lv_event_t* event);
Editor* editorFor(lv_obj_t* keyboard){
    for(uint32_t i=0;i<lv_obj_get_event_count(keyboard);++i){auto* d=lv_obj_get_event_dsc(keyboard,i);
        if(lv_event_dsc_get_cb(d)==editorDeleted)return static_cast<Editor*>(lv_event_dsc_get_user_data(d));}
    return nullptr;
}
void fieldEvent(lv_event_t* event);
void detach(Editor& editor){
    if(editor.field&&lv_obj_is_valid(editor.field))lv_obj_remove_event_cb_with_user_data(editor.field,fieldEvent,&editor);
    editor.field=nullptr;editor.syncing=true;
    lv_textarea_set_password_mode(editor.text,true);lv_textarea_set_text(editor.text,"");
    editor.syncing=false;
}
void editorDeleted(lv_event_t* event){
    auto* editor=static_cast<Editor*>(lv_event_get_user_data(event));
    if(editor->field&&lv_obj_is_valid(editor->field))lv_obj_remove_event_cb_with_user_data(editor->field,fieldEvent,editor);
    delete editor;
}
void editorClosed(lv_event_t* event){
    auto* editor=static_cast<Editor*>(lv_event_get_user_data(event));auto* keyboard=editor->keyboard;
    if(lv_event_get_code(event)==LV_EVENT_READY&&editor->field&&lv_obj_is_valid(editor->field))lv_obj_send_event(editor->field,LV_EVENT_READY,nullptr);
    if(lv_obj_is_valid(keyboard)&&editorFor(keyboard)==editor)detach(*editor);
}
void fieldEvent(lv_event_t* event){
    if(lv_event_get_code(event)!=LV_EVENT_DELETE&&lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED)return;
    auto* editor=static_cast<Editor*>(lv_event_get_user_data(event));
    if(lv_event_get_code(event)==LV_EVENT_DELETE){
        editor->field=nullptr;lv_keyboard_set_textarea(editor->keyboard,nullptr);
        detach(*editor);lv_obj_add_flag(editor->keyboard,LV_OBJ_FLAG_HIDDEN);return;
    }
    if(editor->syncing||!editor->field)return;
    editor->syncing=true;
    lv_textarea_set_text(editor->text,lv_textarea_get_text(editor->field));
    lv_textarea_set_cursor_pos(editor->text,lv_textarea_get_cursor_pos(editor->field));
    editor->syncing=false;
}
void editorChanged(lv_event_t* event){
    auto* editor=static_cast<Editor*>(lv_event_get_user_data(event));
    if(editor->syncing||!editor->field||!lv_obj_is_valid(editor->field))return;
    auto* keyboard=editor->keyboard;auto* field=editor->field;
    const auto cursor=lv_textarea_get_cursor_pos(editor->text);
    editor->syncing=true;
    // Original VALUE_CHANGED callbacks still see the original app-owned field.
    lv_textarea_set_text(field,lv_textarea_get_text(editor->text));
    // A client callback may remove the field or the entire keyboard.
    if(lv_obj_is_valid(keyboard)&&editorFor(keyboard)==editor){
        if(editor->field==field&&lv_obj_is_valid(field)){
            if(strcmp(lv_textarea_get_text(field),lv_textarea_get_text(editor->text))){
                lv_textarea_set_text(editor->text,lv_textarea_get_text(field));
                lv_textarea_set_cursor_pos(editor->text,lv_textarea_get_cursor_pos(field));
            }else lv_textarea_set_cursor_pos(field,cursor);
        }
        editor->syncing=false;
    }
}
void eyeClicked(lv_event_t* event){
    auto* editor=static_cast<Editor*>(lv_event_get_user_data(event));
    const bool masked=!lv_textarea_get_password_mode(editor->text);
    editor->syncing=true;
    lv_textarea_set_password_mode(editor->text,masked);
    editor->syncing=false;
    lv_label_set_text(editor->eyeLabel,masked?LV_SYMBOL_EYE_OPEN:LV_SYMBOL_EYE_CLOSE);
}
void keyboardDeleted(lv_event_t* event) {
    if(activeKeyboard==lv_event_get_target_obj(event))activeKeyboard=nullptr;
}
void outsidePress(lv_event_t* event) {
    auto* keyboard=activeKeyboard;
    if(!keyboard)return;
    if(!lv_obj_is_valid(keyboard)||!lv_obj_is_visible(keyboard)) {activeKeyboard=nullptr;return;}
    auto* indev=static_cast<lv_indev_t*>(lv_event_get_target(event));
    if(lv_indev_get_display(indev)!=lv_obj_get_display(keyboard))return;
    auto* field=lv_keyboard_get_textarea(keyboard);
    auto* target=static_cast<lv_obj_t*>(lv_event_get_param(event));
    lv_point_t point;lv_indev_get_point(indev,&point);
    const auto inside=[&](lv_obj_t* object){lv_area_t area;lv_obj_get_coords(object,&area);return point.x>=area.x1&&point.x<=area.x2&&point.y>=area.y1&&point.y<=area.y2;};
    // A scroll/press-lock can leave the event target on an input although the
    // finger is already outside it. Geometry, not ancestry alone, decides.
    if(inside(keyboard))return;
    if(field&&lv_obj_is_valid(field)) {
        // Input-device events reach us before object callbacks: no bubbling
        // flags, app-specific background handlers or extra polling required.
        for(auto* o=target;o;o=lv_obj_get_parent(o))
            if((o==keyboard||lv_obj_check_type(o,&lv_textarea_class))&&inside(o))return;
    }
    activeKeyboard=nullptr;
    // Let native owners restore their viewport/padding via their existing handler.
    lv_obj_send_event(keyboard,LV_EVENT_CANCEL,nullptr);
    if(lv_obj_is_valid(keyboard)&&lv_obj_check_type(keyboard,&lv_keyboard_class)) {
        lv_keyboard_set_textarea(keyboard,nullptr);
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    }
}
void watch(lv_obj_t* keyboard) {
    bool watched=false;
    for(uint32_t i=0;i<lv_obj_get_event_count(keyboard);++i)
        if(lv_event_dsc_get_cb(lv_obj_get_event_dsc(keyboard,i))==keyboardDeleted)watched=true;
    if(!watched)lv_obj_add_event_cb(keyboard,keyboardDeleted,LV_EVENT_DELETE,nullptr);
    for(auto* indev=lv_indev_get_next(nullptr);indev;indev=lv_indev_get_next(indev)) {
        if(lv_indev_get_type(indev)!=LV_INDEV_TYPE_POINTER)continue;
        bool registered=false;
        for(uint32_t i=0;i<lv_indev_get_event_count(indev);++i)
            if(lv_event_dsc_get_cb(lv_indev_get_event_dsc(indev,i))==outsidePress)registered=true;
        if(!registered){
            lv_indev_add_event_cb(indev,outsidePress,LV_EVENT_PRESSED,nullptr);
            lv_indev_add_event_cb(indev,outsidePress,LV_EVENT_RELEASED,nullptr);
        }
    }
    activeKeyboard=keyboard;
}
// Object-local button maps: lv_keyboard_set_map would modify LVGL's global maps.
const char* const integerMap[]={"7","8","9",LV_SYMBOL_BACKSPACE,"\n","4","5","6",LV_SYMBOL_LEFT,"\n","1","2","3",LV_SYMBOL_RIGHT,"\n",LV_SYMBOL_CLOSE,"0","00",LV_SYMBOL_OK,""};
const char* const decimalMap[]={"7","8","9",LV_SYMBOL_BACKSPACE,"\n","4","5","6",LV_SYMBOL_LEFT,"\n","1","2","3",".","\n",LV_SYMBOL_CLOSE,"0",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
const char* const signedMap[]={"7","8","9",LV_SYMBOL_BACKSPACE,"\n","4","5","6","-","\n","1","2","3",".","\n",LV_SYMBOL_CLOSE,"0",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
const char* const phoneMap[]={"7","8","9",LV_SYMBOL_BACKSPACE,"\n","4","5","6","+","\n","1","2","3","-","\n","*","0","#"," ","\n",LV_SYMBOL_CLOSE,"(",")",LV_SYMBOL_OK,""};
const char* const dateMap[]={"7","8","9",LV_SYMBOL_BACKSPACE,"\n","4","5","6","-","\n","1","2","3",":","\n",".","0","/"," ","\n",LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT,LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
constexpr lv_buttonmatrix_ctrl_t key=static_cast<lv_buttonmatrix_ctrl_t>(1|LV_BUTTONMATRIX_CTRL_NO_REPEAT|LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
constexpr lv_buttonmatrix_ctrl_t erase=static_cast<lv_buttonmatrix_ctrl_t>(1|LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
const lv_buttonmatrix_ctrl_t controls[]={key,key,key,erase,key,key,key,key,key,key,key,key,key,key,key,key,key,key,key,key};
}
bool prepare(lv_obj_t* keyboard){
    if(!keyboard||!lv_obj_check_type(keyboard,&lv_keyboard_class))return false;
    if(editorFor(keyboard))return true;
    auto* editor=new(std::nothrow) Editor;if(!editor)return false;
    editor->keyboard=keyboard;
    auto* strip=lv_obj_create(keyboard);if(!strip){delete editor;return false;}UiTheme::instance().styleInput(strip);
    lv_obj_set_size(strip,LV_PCT(100),56);lv_obj_set_pos(strip,0,-64);
    lv_obj_remove_flag(strip,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_pad_all(strip,4,0);
    lv_obj_set_flex_flow(strip,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(strip,8,0);
    editor->text=lv_textarea_create(strip);if(!editor->text){lv_obj_delete(strip);delete editor;return false;}UiTheme::instance().styleInput(editor->text);
    lv_obj_set_size(editor->text,0,44);lv_obj_set_flex_grow(editor->text,1);
    lv_obj_set_style_text_font(editor->text,fontWithGermanFallback(&lv_font_montserrat_18),0);
    lv_textarea_set_password_show_time(editor->text,0);
    lv_obj_add_event_cb(editor->text,editorChanged,LV_EVENT_VALUE_CHANGED,editor);
    editor->eye=lv_button_create(strip);if(!editor->eye){lv_obj_delete(strip);delete editor;return false;}UiTheme::instance().styleRoundIcon(editor->eye);
    lv_obj_set_size(editor->eye,48,44);
    editor->eyeLabel=lv_label_create(editor->eye);if(!editor->eyeLabel){lv_obj_delete(strip);delete editor;return false;}lv_label_set_text(editor->eyeLabel,LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(editor->eyeLabel,fontWithGermanFallback(&lv_font_montserrat_24),0);lv_obj_center(editor->eyeLabel);
    lv_obj_add_event_cb(editor->eye,eyeClicked,LV_EVENT_CLICKED,editor);
    lv_obj_add_event_cb(keyboard,editorDeleted,LV_EVENT_DELETE,editor);
    lv_obj_add_event_cb(keyboard,editorClosed,LV_EVENT_CANCEL,editor);
    lv_obj_add_event_cb(keyboard,editorClosed,LV_EVENT_READY,editor);
    return true;
}
void hide(lv_obj_t* keyboard){
    if(!keyboard||!lv_obj_is_valid(keyboard)||!lv_obj_check_type(keyboard,&lv_keyboard_class))return;
    if(auto* editor=editorFor(keyboard))detach(*editor);
    lv_keyboard_set_textarea(keyboard,nullptr);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    if(activeKeyboard==keyboard)activeKeyboard=nullptr;
}
void setMode(lv_obj_t* field,keyboardInput::Mode mode) {
    if(field && lv_obj_check_type(field,&lv_textarea_class))
        lv_textarea_set_accepted_chars(field,keyboardInput::accepted(mode));
}
void bind(lv_obj_t* keyboard,lv_obj_t* field) {
    if(!keyboard || !field || !lv_obj_check_type(keyboard,&lv_keyboard_class) || !lv_obj_check_type(field,&lv_textarea_class)) return;
    const auto mode=keyboardInput::fromAccepted(lv_textarea_get_accepted_chars(field));
    watch(keyboard);
    auto* editor=prepare(keyboard)?editorFor(keyboard):nullptr;
    if(editor){
        detach(*editor);editor->field=field;editor->syncing=true;
        lv_textarea_set_one_line(editor->text,lv_textarea_get_one_line(field));
        lv_textarea_set_max_length(editor->text,lv_textarea_get_max_length(field));
        lv_textarea_set_accepted_chars(editor->text,lv_textarea_get_accepted_chars(field));
        lv_textarea_set_password_mode(editor->text,lv_textarea_get_password_mode(field));
        lv_textarea_set_text(editor->text,lv_textarea_get_text(field));
        lv_textarea_set_cursor_pos(editor->text,lv_textarea_get_cursor_pos(field));
        editor->syncing=false;
        lv_obj_add_event_cb(field,fieldEvent,LV_EVENT_ALL,editor);
        if(lv_textarea_get_password_mode(field))lv_obj_remove_flag(editor->eye,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(editor->eye,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(editor->eyeLabel,LV_SYMBOL_EYE_OPEN);
        lv_obj_set_height(keyboard,355);lv_obj_set_style_pad_top(keyboard,72,0);
        // A tall non-scrollable app page must not push the keyboard/editor
        // beyond the display. Keep this shared editing surface above it.
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_FLOATING);
        lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_move_foreground(keyboard);
    }
    lv_keyboard_set_textarea(keyboard,editor?editor->text:field);
    lv_keyboard_set_mode(keyboard,mode==keyboardInput::Mode::Text ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_NUMBER);
    if(mode!=keyboardInput::Mode::Text){
    const char* const* map=integerMap;
    switch(mode) {
    case keyboardInput::Mode::Decimal: map=decimalMap; break;
    case keyboardInput::Mode::SignedDecimal: map=signedMap; break;
    case keyboardInput::Mode::Phone: map=phoneMap; break;
    case keyboardInput::Mode::DateTime: map=dateMap; break;
    default: break;
    }
    lv_buttonmatrix_set_map(keyboard,map);
    lv_buttonmatrix_set_ctrl_map(keyboard,controls);
    }
    auto& theme=UiTheme::instance();
    lv_obj_set_style_bg_color(keyboard,theme.backgroundAlt(),LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard,theme.card(),LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,theme.textPrimary(),LV_PART_ITEMS);
    constexpr auto pressed=static_cast<lv_style_selector_t>(LV_PART_ITEMS)|static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(keyboard,theme.accent(),pressed);
    lv_obj_set_style_text_color(keyboard,theme.onAccent(),pressed);
    lv_obj_set_style_radius(keyboard,10,LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard,fontWithGermanFallback(&lv_font_montserrat_24),LV_PART_ITEMS);
}
}
