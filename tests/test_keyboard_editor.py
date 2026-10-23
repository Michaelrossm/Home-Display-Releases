from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]


class KeyboardEditorTests(unittest.TestCase):
    def test_real_editor_sync_mask_reset_normalization_and_owner_deletion(self):
        source=(ROOT/'src/ui/InputKeyboard.cpp').read_text(encoding='utf-8')
        methods='struct Editor {'+source.split('struct Editor {',1)[1].split('void keyboardDeleted(',1)[0]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
struct lv_event_t;using Callback=void(*)(lv_event_t*);
struct Descriptor{Callback cb;int code;void* data;};
struct lv_obj_t{std::string text;std::vector<Descriptor> events;bool valid=true,masked=true,hidden=false;int cursor=0;lv_obj_t* textarea=nullptr;};
struct lv_event_t{lv_obj_t* target;int code;void* data;};
constexpr int LV_EVENT_ALL=0,LV_EVENT_DELETE=1,LV_EVENT_VALUE_CHANGED=2,LV_EVENT_READY=3,LV_EVENT_CANCEL=4,LV_OBJ_FLAG_HIDDEN=5;
const char* LV_SYMBOL_EYE_OPEN="open";const char* LV_SYMBOL_EYE_CLOSE="closed";
unsigned lv_obj_get_event_count(lv_obj_t* o){return o->events.size();}
Descriptor* lv_obj_get_event_dsc(lv_obj_t* o,unsigned i){return &o->events[i];}
Callback lv_event_dsc_get_cb(Descriptor* d){return d->cb;}void* lv_event_dsc_get_user_data(Descriptor* d){return d->data;}
void* lv_event_get_user_data(lv_event_t* e){return e->data;}int lv_event_get_code(lv_event_t* e){return e->code;}
bool lv_obj_is_valid(lv_obj_t* o){return o&&o->valid;}
void lv_obj_remove_event_cb_with_user_data(lv_obj_t* o,Callback cb,void* data){
 o->events.erase(std::remove_if(o->events.begin(),o->events.end(),[&](auto d){return d.cb==cb&&d.data==data;}),o->events.end());}
void lv_obj_send_event(lv_obj_t* o,int code,void*){
 auto list=o->events;for(auto d:list){if(!o->valid)break;
  bool live=false;for(auto current:o->events)if(current.cb==d.cb&&current.data==d.data&&current.code==d.code)live=true;
  if(live&&(d.code==LV_EVENT_ALL||d.code==code)){lv_event_t e{o,code,d.data};d.cb(&e);}}
}
const char* lv_textarea_get_text(lv_obj_t* o){return o->text.c_str();}
void lv_textarea_set_text(lv_obj_t* o,const char* text){o->text=text;o->cursor=o->text.size();lv_obj_send_event(o,LV_EVENT_VALUE_CHANGED,nullptr);}
void lv_textarea_set_password_mode(lv_obj_t* o,bool v){o->masked=v;}bool lv_textarea_get_password_mode(lv_obj_t* o){return o->masked;}
int lv_textarea_get_cursor_pos(lv_obj_t* o){return o->cursor;}void lv_textarea_set_cursor_pos(lv_obj_t* o,int p){o->cursor=p;}
void lv_keyboard_set_textarea(lv_obj_t* k,lv_obj_t* f){k->textarea=f;}
void lv_obj_add_flag(lv_obj_t* o,int){o->hidden=true;}void lv_label_set_text(lv_obj_t* o,const char* t){o->text=t;}
'''+methods+r'''
int changes=0,ready=0;lv_obj_t* keyboardToDelete=nullptr;
void client(lv_event_t* event){
 if(event->code==LV_EVENT_READY){++ready;return;}
 if(event->code!=LV_EVENT_VALUE_CHANGED)return;++changes;
 if(event->target->text=="19961224")lv_textarea_set_text(event->target,"1996-12-24");
 if(keyboardToDelete){auto* k=keyboardToDelete;keyboardToDelete=nullptr;lv_obj_send_event(k,LV_EVENT_DELETE,nullptr);k->valid=false;}
}
Editor* setup(lv_obj_t& keyboard,lv_obj_t& field,lv_obj_t& text,lv_obj_t& eye){
 auto* editor=new Editor;editor->keyboard=&keyboard;editor->field=&field;editor->text=&text;editor->eyeLabel=&eye;
 keyboard.events={{editorDeleted,LV_EVENT_DELETE,editor}};field.events={{client,LV_EVENT_ALL,nullptr},{fieldEvent,LV_EVENT_ALL,editor}};
 text.events={{editorChanged,LV_EVENT_VALUE_CHANGED,editor}};return editor;
}
int main(){
 lv_obj_t keyboard,field,text,eye;auto* editor=setup(keyboard,field,text,eye);lv_event_t event{&keyboard,LV_EVENT_CANCEL,editor};
 lv_textarea_set_text(&text,u8"Übung\nzweite Zeile");assert(field.text==u8"Übung\nzweite Zeile"&&changes==1);
 eyeClicked(&event);assert(!text.masked&&field.masked);eyeClicked(&event);assert(text.masked);
 lv_textarea_set_text(&text,"19961224");assert(field.text=="1996-12-24"&&text.text==field.text);
 editorClosed(&event);assert(text.text.empty()&&text.masked&&!editor->field&&field.events.size()==1);
 lv_obj_send_event(&keyboard,LV_EVENT_DELETE,nullptr);assert(field.events.size()==1);
 editor=setup(keyboard,field,text,eye);event.data=editor;event.code=LV_EVENT_READY;editorClosed(&event);assert(ready==1&&text.text.empty());
 lv_obj_send_event(&keyboard,LV_EVENT_DELETE,nullptr);
 editor=setup(keyboard,field,text,eye);lv_obj_send_event(&field,LV_EVENT_DELETE,nullptr);assert(!editor->field&&keyboard.hidden&&text.text.empty());
 lv_obj_send_event(&keyboard,LV_EVENT_DELETE,nullptr);
 setup(keyboard,field,text,eye);keyboardToDelete=&keyboard;lv_textarea_set_text(&text,"client deletes UI");assert(!keyboard.valid);
}
''')

    def test_keyboard_strip_is_bounded_and_accounted(self):
        source=(ROOT/'src/ui/InputKeyboard.cpp').read_text(encoding='utf-8')
        for expression in ('lv_textarea_get_max_length(field)','lv_textarea_get_one_line(field)',
                           'lv_textarea_get_accepted_chars(field)','lv_textarea_get_password_mode(field)',
                           'lv_textarea_set_password_show_time(editor->text,0)', 'lv_obj_set_style_pad_top(keyboard,72,0)',
                           'lv_obj_add_flag(keyboard,LV_OBJ_FLAG_FLOATING)'):
            self.assertIn(expression,source)
        self.assertNotIn('xTaskCreate',source)
        bridge=(ROOT/'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('guard(L,"hp_ui_keyboard",true,5)',bridge)
        self.assertIn('InputKeyboard::hide(kb)',bridge)


if __name__=='__main__':unittest.main()
