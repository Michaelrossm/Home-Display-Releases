from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class NativeAppIconTests(unittest.TestCase):
    def test_actual_vector_primitives_and_updates(self):
        code=(ROOT/'src/ui/NativeAppIcon.h').read_text(encoding='utf-8').replace('#pragma once','').replace('#include "UiTheme.h"','')
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cstdint>
#include <vector>
#include <set>
#include <cassert>
#include <algorithm>
using lv_color_t=uint32_t;
struct lv_area_t{int x1,y1,x2,y2;};
struct lv_obj_t;struct lv_event_t;
using Callback=void(*)(lv_event_t*);
struct lv_obj_t{int w=48,h=48;void* data=nullptr;lv_color_t color=0;Callback cb=nullptr;};
struct lv_event_t{lv_obj_t* object;};
struct lv_draw_rect_dsc_t{lv_color_t bg_color,border_color;int bg_opa,border_width,border_opa,radius;};
constexpr int LV_OPA_COVER=255,LV_OPA_TRANSP=0,LV_OBJ_FLAG_CLICKABLE=1,LV_OBJ_FLAG_SCROLLABLE=2,LV_EVENT_DRAW_MAIN=3;
constexpr const char* LV_SYMBOL_LIST="list";
int lv_font_montserrat_48,lv_font_montserrat_24;
int* fontWithGermanFallback(int* p){return p;}
lv_obj_t pool[256];int created=0,invalidations=0;
std::vector<lv_area_t> rectangles;std::set<lv_color_t> colors;
lv_obj_t* lv_obj_create(lv_obj_t*){return &pool[created++];}
lv_obj_t* lv_label_create(lv_obj_t* p){return lv_obj_create(p);}
void lv_label_set_text(lv_obj_t*,const char*){}
void lv_obj_set_style_text_font(lv_obj_t*,int*,int){}
void lv_obj_set_style_text_color(lv_obj_t* o,lv_color_t c,int){o->color=c;}
void lv_obj_remove_style_all(lv_obj_t*){}
void lv_obj_set_size(lv_obj_t* o,int w,int h){o->w=w;o->h=h;}
void lv_obj_remove_flag(lv_obj_t*,int){}
void lv_obj_set_user_data(lv_obj_t* o,void* d){o->data=d;}
void* lv_obj_get_user_data(lv_obj_t* o){return o->data;}
void lv_obj_invalidate(lv_obj_t*){++invalidations;}
void lv_obj_add_event_cb(lv_obj_t* o,Callback cb,int,void*){o->cb=cb;}
lv_obj_t* lv_event_get_target_obj(lv_event_t* e){return e->object;}
void* lv_event_get_layer(lv_event_t*){return nullptr;}
void lv_obj_get_coords(lv_obj_t* o,lv_area_t* a){*a={0,0,o->w-1,o->h-1};}
int lv_obj_get_width(lv_obj_t* o){return o->w;}
int lv_obj_get_height(lv_obj_t* o){return o->h;}
lv_color_t lv_obj_get_style_text_color(lv_obj_t* o,int){return o->color;}
lv_color_t lv_color_hex(uint32_t c){return c;}
void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t* d){*d={};}
void lv_draw_rect(void*,lv_draw_rect_dsc_t* d,lv_area_t* a){rectangles.push_back(*a);colors.insert(d->bg_color);}
''' + code + r'''
int main(){
 for(const char* symbol:{"@weather","@weather_clear","@weather_clear_night","@weather_cloudy","@weather_rain","@weather_storm","@weather_snow","@weather_fog","@weather_wind","@printer","@contacts","@intercom","@calculator","@photos","@settings","@drop","@calendar","@rain_drops","@radio","@files","@speaker","@speaker_muted","@heart","@heart_outline","@energy","@solar","@house","@battery"}){
  for(int size:{24,32,48,64}){
   auto* icon=hp::createAppIcon(nullptr,symbol,size,0x123456);assert(icon->cb);
   rectangles.clear();colors.clear();lv_event_t event{icon};icon->cb(&event);assert(!rectangles.empty());
   for(auto a:rectangles){assert(a.x1>=0&&a.y1>=0&&a.x2<=size&&a.y2<=size);assert(a.x2>=a.x1&&a.y2>=a.y1);}
   if(hp::appIconKind(symbol)==3||hp::appIconKind(symbol)==12||hp::appIconKind(symbol)==1||hp::appIconKind(symbol)==2||hp::appIconKind(symbol)==16)assert(colors.size()>=3);
   if(hp::appIconKind(symbol)==4)assert(rectangles.size()>=10);
  }
 }
 auto* icon=hp::createAppIcon(nullptr,"@weather_cloudy",48,0);int count=created;
 assert(hp::setAppIconSymbol(icon,"@weather_rain"));assert(created==count&&invalidations==1);
 assert(hp::setAppIconSymbol(icon,"@weather_rain")&&invalidations==1);
 assert(!hp::setAppIconSymbol(icon,"invalid"));lv_obj_t arbitrary;
 assert(!hp::setAppIconSymbol(&arbitrary,"@printer"));
}
''')

    def test_registry_maps_semantic_manifest_icons(self):
        registry=(ROOT/'src/apps/lua/LuaAppRegistry.cpp').read_text(encoding='utf-8')
        self.assertIn('raw == "weather") return "@weather"',registry)
        self.assertIn('raw == "printer" || raw == "3d_printer"',registry)

    def test_calendar_uses_shared_colored_vector(self):
        app=(ROOT/'src/apps/calendar/CalendarApp.h').read_text(encoding='utf-8')
        self.assertIn('return "@calendar";',app)
        renderer=(ROOT/'src/ui/NativeAppIcon.h').read_text(encoding='utf-8')
        self.assertIn('"@calendar"',renderer)
