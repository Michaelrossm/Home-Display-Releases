from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]


class SharedNavigationTests(unittest.TestCase):
    def test_page_drop_target_uses_visible_enabled_apps(self):
        source=(ROOT/'src/ui/HomeReorderController.cpp').read_text(encoding='utf-8')
        body=source.split('if(pageDirection){',1)[1].split('pendingAfter_=pageDirection>0;',1)[0]
        helper=test_system_performance.SystemPerformanceTests()
        helper.native(helper.string_fixture()+r'''
enum class AppRegistryState{Enabled,Disabled};
struct Entry{String id;bool launcherVisible=true;AppRegistryState state=AppRegistryState::Enabled;};
struct AppRegistry{std::vector<Entry> entries;static AppRegistry& instance(){static AppRegistry r;return r;}
 void rebuild(){}size_t count(){return entries.size();}Entry* at(size_t i){return &entries[i];}};
struct UiShell{int page=0;static UiShell& instance(){static UiShell s;return s;}int desktopPage(){return page;}};
String select(int pageDirection){String target;
'''+body+r'''
return target;}
int main(){auto& r=AppRegistry::instance();for(int i=0;i<23;++i)r.entries.push_back({"app"+String(i)});
 assert(select(1)=="app19");UiShell::instance().page=1;assert(select(-1)=="app0"&&select(1)=="app22");
 UiShell::instance().page=2;assert(select(1).empty());UiShell::instance().page=0;assert(select(-1).empty());
 r.entries[0].launcherVisible=false;r.entries[1].state=AppRegistryState::Disabled;assert(select(1)=="app21");
}
''')

    def test_cross_page_drop_is_persisted_after_release(self):
        source=(ROOT/'src/ui/HomeReorderController.cpp').read_text(encoding='utf-8')
        release=source.split('void HomeReorderController::finishDrag(',1)[1].split('void HomeReorderController::cancelDrag(',1)[0]
        self.assertIn('pendingPageDirection_=direction',release)
        self.assertIn('cancelDrag(binding)',release)
        self.assertNotIn('HomeAppOrderService::instance().move',release)
        self.assertIn('requestDesktopPage(pageDirection)',source)

    def test_page_changes_are_deferred_and_not_allowed_during_editing(self):
        source = (ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        callback = source.split('void UiShell::homePageBtn',1)[1].split('void UiShell::changeHomePage',1)[0]
        self.assertNotIn('rebuildHome()',callback)
        self.assertIn('pendingHomePageChange_.store',callback)
        self.assertIn('LV_DIR_LEFT',callback)
        self.assertIn('LV_DIR_RIGHT',callback)
        self.assertIn('s.desktopEditing_',callback)
        self.assertIn('changeHomePage(pendingHomePageChange_.exchange(0))',source)
        self.assertIn('lv_obj_remove_flag(home_,LV_OBJ_FLAG_GESTURE_BUBBLE)',source)
        self.assertIn('pageCount*24',source)
        self.assertNotIn('LV_ALIGN_TOP_RIGHT,-126,25',source)

    def test_settings_back_is_owned_by_system_bar(self):
        source = (ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        self.assertIn('makeSymbolButton(top_,LV_SYMBOL_LEFT " Einstellungen"',source)
        self.assertNotIn('lv_label_set_text(label,LV_SYMBOL_LEFT " Apps")',source)
        branch = source.split('if(AppManager::instance().openSettings(appId+10,content_))',1)[1].split('else openAppsSettingsList()',1)[0]
        self.assertNotIn('lv_button_create(content_)',branch)
        self.assertIn('setHiddenIfChanged(appSettingsBack_,false)',branch)

    def test_shared_input_style_has_no_fixed_dark_palette(self):
        bridge = (ROOT/'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('UiTheme::instance().styleInput(ta)',bridge)
        textarea = bridge.split('int bridgeTextarea(',1)[1].split('int bridgeTextareaGet(',1)[0]
        self.assertNotIn('0x0A1625',textarea)
        widgets = (ROOT/'src/ui/SettingsWidgets.h').read_text(encoding='utf-8')
        self.assertIn('UiTheme::instance().styleInput(obj)',widgets)

    def test_keyboard_outside_touch_and_stale_input_target(self):
        source = (ROOT/'src/ui/InputKeyboard.cpp').read_text(encoding='utf-8')
        body = source.split('void outsidePress(lv_event_t* event) {',1)[1].split('\nvoid watch(',1)[0]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cassert>
struct lv_area_t{int x1,y1,x2,y2;};struct lv_point_t{int x,y;};
struct lv_obj_t{lv_area_t area;lv_obj_t* parent=nullptr;bool input=false,valid=true,visible=true;};
struct lv_indev_t{lv_point_t point;};struct lv_event_t{lv_indev_t* indev;lv_obj_t* target;};
int lv_textarea_class=1,lv_keyboard_class=2;constexpr int LV_EVENT_CANCEL=3,LV_OBJ_FLAG_HIDDEN=4;
lv_obj_t* activeKeyboard;lv_obj_t* field;int cancelled=0;
bool lv_obj_is_valid(lv_obj_t* o){return o->valid;}bool lv_obj_is_visible(lv_obj_t* o){return o->visible;}
int lv_indev_get_display(lv_indev_t*){return 1;}int lv_obj_get_display(lv_obj_t*){return 1;}
lv_obj_t* lv_keyboard_get_textarea(lv_obj_t*){return field;}
void* lv_event_get_target(lv_event_t* e){return e->indev;}void* lv_event_get_param(lv_event_t* e){return e->target;}
void lv_indev_get_point(lv_indev_t* d,lv_point_t* p){*p=d->point;}
void lv_obj_get_coords(lv_obj_t* o,lv_area_t* a){*a=o->area;}
lv_obj_t* lv_obj_get_parent(lv_obj_t* o){return o->parent;}
bool lv_obj_check_type(lv_obj_t* o,int* type){return type==&lv_keyboard_class||o->input;}
void lv_obj_send_event(lv_obj_t*,int,void*){++cancelled;}
void lv_keyboard_set_textarea(lv_obj_t*,lv_obj_t* o){field=o;}
void lv_obj_add_flag(lv_obj_t* o,int){o->visible=false;}
void outsidePress(lv_event_t* event){
''' + body + r'''
int main(){
 lv_obj_t keyboard{{0,500,1000,800}},input{{20,20,400,80},nullptr,true},background{{0,0,1000,800}};
 lv_indev_t device{{30,30}};lv_event_t event{&device,&input};field=&input;activeKeyboard=&keyboard;
 outsidePress(&event);assert(cancelled==0);
 device.point={40,600};event.target=&keyboard;outsidePress(&event);assert(cancelled==0);
 device.point={800,200};event.target=&input;outsidePress(&event);assert(cancelled==1&&!activeKeyboard&&!keyboard.visible&&!field);
 keyboard.visible=true;field=&input;activeKeyboard=&keyboard;event.target=&background;
 outsidePress(&event);assert(cancelled==2);
 keyboard.valid=false;activeKeyboard=&keyboard;outsidePress(&event);assert(!activeKeyboard&&cancelled==2);
}
''')


if __name__ == '__main__':
    unittest.main()
