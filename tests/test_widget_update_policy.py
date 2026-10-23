from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class WidgetUpdatePolicyTests(unittest.TestCase):
    def test_local_style_diff_keeps_first_override_and_skips_identical_setters(self):
        source=(ROOT/'src/apps/lua/LuaBasicUiBridge.cpp').read_text(encoding='utf-8')
        helpers=source[source.index('void localNumber('):source.index('lv_align_t parseAlign(')]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <map>
#include <cstdint>
#include <cassert>
using lv_style_prop_t=int;using lv_color_t=uint32_t;
struct lv_font_t{};
union lv_style_value_t{int32_t num;lv_color_t color;const void* ptr;};
struct lv_obj_t{std::map<int,lv_style_value_t> values;unsigned writes=0;};
constexpr int LV_PART_MAIN=0,LV_STYLE_RES_FOUND=1,LV_STYLE_TEXT_FONT=10;
bool lv_color_eq(lv_color_t a,lv_color_t b){return a==b;}
int lv_obj_get_local_style_prop(lv_obj_t* o,int prop,lv_style_value_t* v,int){auto i=o->values.find(prop);if(i==o->values.end())return 0;*v=i->second;return 1;}
void lv_obj_set_local_style_prop(lv_obj_t* o,int prop,lv_style_value_t v,int){o->values[prop]=v;++o->writes;}
struct UiPerformanceMonitor{unsigned changed=0,skipped=0;static UiPerformanceMonitor& instance(){static UiPerformanceMonitor m;return m;}void widgetUpdate(bool c){++(c?changed:skipped);}};
'''+helpers+r'''
int main(){lv_obj_t o;lv_font_t font;
localNumber(&o,1,0);assert(o.writes==1); // Explicit zero must override the theme.
for(unsigned i=0;i<1000;++i)localNumber(&o,1,0);assert(o.writes==1);
localNumber(&o,1,2);localColor(&o,2,0x123456);localFont(&o,&font);assert(o.writes==4);
for(unsigned i=0;i<1000;++i){localColor(&o,2,0x123456);localFont(&o,&font);}assert(o.writes==4);
assert(UiPerformanceMonitor::instance().changed==4&&UiPerformanceMonitor::instance().skipped==3000);
}
''')

    def test_refresh_caps_are_not_minimum_frame_rates(self):
        header=(ROOT/'src/core/UiRefreshPolicy.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(header+r'''
#include <cassert>
int main(){using hp::uiRefreshPeriod;
assert(uiRefreshPeriod(true,false,false,true,true)==0);
assert(uiRefreshPeriod(false,false,false,true,false)==33);
assert(uiRefreshPeriod(false,true,false,false,true)==200);
assert(uiRefreshPeriod(false,false,true,false,false)==100);
assert(uiRefreshPeriod(false,false,false,false,true)==33);
assert(uiRefreshPeriod(false,false,false,false,false)==100);
}
''')

if __name__=='__main__':unittest.main()
