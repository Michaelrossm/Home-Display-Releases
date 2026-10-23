from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class ChartCursorTests(unittest.TestCase):
    def test_actual_touch_handler_and_missing_values(self):
        source=(ROOT/'src/apps/lua/LuaChartBridge.cpp').read_text(encoding='utf-8')
        code=source[source.index('void updateCursor('):source.index('int create(')]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <algorithm>
#include <array>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cassert>
struct lv_chart_cursor_t{};struct lv_chart_series_t{};struct lv_obj_t{};
struct lv_point_t{int32_t x,y;};struct lv_area_t{int x1,y1,x2,y2;};
struct lv_indev_t{int type=1;lv_point_t point{20,0};};
struct lv_event_t{lv_obj_t* object;lv_indev_t* input;};
struct Chart{
 lv_chart_series_t* handles[4]{};lv_obj_t* cursorValue=nullptr;lv_chart_cursor_t* cursor=nullptr;
 unsigned cursorCount=3,cursorIndex=0;std::array<std::string,32> cursorLabels;std::string cursorSuffix=" %";
};
constexpr int LV_CHART_POINT_NONE=INT32_MAX,LV_INDEV_TYPE_POINTER=1;
Chart state;int32_t values[]={0,75,LV_CHART_POINT_NONE};std::string tooltip;int writes=0,selected=-1,position=-1;
Chart* chartState(lv_obj_t*){return &state;}
unsigned lv_chart_get_point_count(lv_obj_t*){return 3;}
int32_t* lv_chart_get_y_array(lv_obj_t*,lv_chart_series_t*){return values;}
void lv_chart_set_cursor_point(lv_obj_t*,lv_chart_cursor_t*,lv_chart_series_t*,unsigned i){selected=i;}
int lv_obj_get_content_width(lv_obj_t*){return 200;}
void lv_chart_set_cursor_pos(lv_obj_t*,lv_chart_cursor_t*,lv_point_t* p){position=p->x;}
void lv_label_set_text_fmt(lv_obj_t*,const char* format,...){char text[128];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);tooltip=text;++writes;}
lv_obj_t* lv_event_get_target_obj(lv_event_t* e){return e->object;}
lv_indev_t* lv_event_get_indev(lv_event_t* e){return e->input;}
int lv_indev_get_type(lv_indev_t* i){return i->type;}
void lv_indev_get_point(lv_indev_t* i,lv_point_t* p){*p=i->point;}
void lv_obj_get_content_coords(lv_obj_t*,lv_area_t* a){*a={20,0,220,100};}
''' + code + r'''
int main(){
 lv_obj_t chart,label;lv_chart_series_t series;lv_chart_cursor_t cursor;lv_indev_t input;lv_event_t e{&chart,&input};
 state.cursorValue=&label;state.handles[0]=&series;state.cursor=&cursor;
 state.cursorLabels[0]="00:00";state.cursorLabels[1]="01:00";state.cursorLabels[2]="02:00";
 updateCursor(&chart,&state,0);assert(tooltip=="00:00 · 0 %"&&selected==0);
 input.point.x=120;dragCursor(&e);assert(tooltip=="01:00 · 75 %"&&selected==1);
 const int before=writes;dragCursor(&e);assert(writes==before);
 input.point.x=999;dragCursor(&e);assert(state.cursorIndex==2&&position==200&&tooltip=="02:00 · Keine Daten");
 input.point.x=-999;dragCursor(&e);assert(state.cursorIndex==0);
 input.type=2;input.point.x=120;dragCursor(&e);assert(state.cursorIndex==0);
 e.input=nullptr;dragCursor(&e);assert(state.cursorIndex==0);
}
''')
