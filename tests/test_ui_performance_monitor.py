"""Host-side tests for the passive LVGL performance observer."""
import subprocess
import shutil
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class UiPerformanceMonitorTests(unittest.TestCase):
    def test_render_and_navigation_metrics(self):
        with tempfile.TemporaryDirectory() as td:
            t = Path(td)
            (t / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
uint32_t millis();
''')
            (t / 'esp_timer.h').write_text('''#pragma once
#include <cstdint>
int64_t esp_timer_get_time();
''')
            (t / 'lvgl.h').write_text('''#pragma once
#include <cstdint>
struct lv_event_t { int code; void* data; void* param=nullptr; };
struct lv_area_t {int32_t x1,y1,x2,y2;};
struct lv_timer_t {};
struct lv_obj_t {};
inline bool timerPaused=false;inline uint32_t timerPeriod=0;inline unsigned timerReady=0,invalidated=0,animationCount=0;
struct lv_display_t {};
inline lv_timer_t* lv_display_get_refr_timer(lv_display_t*){static lv_timer_t t;return &t;}
inline void lv_timer_set_period(lv_timer_t*,uint32_t p){timerPeriod=p;}
inline void lv_timer_pause(lv_timer_t*){timerPaused=true;}
inline void lv_timer_resume(lv_timer_t*){timerPaused=false;}
inline void lv_timer_ready(lv_timer_t*){++timerReady;}
inline unsigned lv_anim_count_running(){return animationCount;}
inline lv_obj_t* lv_display_get_screen_active(lv_display_t*){return nullptr;}
inline void lv_obj_invalidate(lv_obj_t*){++invalidated;}
inline void* lv_event_get_param(lv_event_t* e){return e->param;}
struct lv_indev_t {};
using lv_event_code_t=int;
using lv_event_cb_t=void (*)(lv_event_t*);
constexpr int LV_EVENT_ALL=0;
constexpr int LV_EVENT_PRESSED=1;
constexpr int LV_EVENT_PRESSING=8;
constexpr int LV_EVENT_REFR_START=2;
constexpr int LV_EVENT_RENDER_START=3;
constexpr int LV_EVENT_RENDER_READY=4;
constexpr int LV_EVENT_FLUSH_START=5;
constexpr int LV_EVENT_FLUSH_FINISH=6;
constexpr int LV_EVENT_REFR_READY=7;
constexpr int LV_INDEV_TYPE_POINTER=1;
int lv_event_get_code(lv_event_t*);
void* lv_event_get_user_data(lv_event_t*);
void lv_display_add_event_cb(lv_display_t*,lv_event_cb_t,int,void*);
lv_indev_t* lv_indev_get_next(lv_indev_t*);
int lv_indev_get_type(lv_indev_t*);
void lv_indev_add_event_cb(lv_indev_t*,lv_event_cb_t,int,void*);
''')
            (t / 'test.cpp').write_text('''#include "UiPerformanceMonitor.h"
#include <cassert>
using namespace hp;
static int64_t clockUs=1000000;
static lv_event_cb_t eventCB=nullptr,touchCB=nullptr;
static void* eventUser=nullptr,*touchUser=nullptr;
uint32_t millis(){return clockUs/1000;}
int64_t esp_timer_get_time(){return clockUs;}
int lv_event_get_code(lv_event_t* e){return e->code;}
void* lv_event_get_user_data(lv_event_t* e){return e->data;}
void lv_display_add_event_cb(lv_display_t*,lv_event_cb_t cb,int,void* ud){eventCB=cb;eventUser=ud;}
lv_indev_t* lv_indev_get_next(lv_indev_t* previous){static lv_indev_t indev;return previous?nullptr:&indev;}
int lv_indev_get_type(lv_indev_t*){return LV_INDEV_TYPE_POINTER;}
void lv_indev_add_event_cb(lv_indev_t*,lv_event_cb_t cb,int,void* ud){touchCB=cb;touchUser=ud;}
void send(int code){lv_event_t e{code,eventUser};eventCB(&e);}
int main(){
    lv_display_t display;
    auto& p=UiPerformanceMonitor::instance();p.attach(&display);
    assert(eventCB&&touchCB);
    lv_event_t touch{LV_EVENT_PRESSED,touchUser};touchCB(&touch);
    clockUs+=5000;p.navigationQueued();
    clockUs+=12000;p.navigationBuilt();
    send(LV_EVENT_REFR_START);
    send(LV_EVENT_RENDER_START);clockUs+=4000;send(LV_EVENT_RENDER_READY);
    send(LV_EVENT_FLUSH_START);clockUs+=1000;send(LV_EVENT_FLUSH_FINISH);
    send(LV_EVENT_REFR_READY);
    assert(p.render().samples==1);
    assert(p.flushCallback().samples==1);
    assert(p.completedRefreshes()==1);
    assert(p.visibleNavigation().samples==1);
    assert(p.render().maxUs==4000);
    assert(p.visibleNavigation().maxUs>=22000);
    p.navigationQueued();clockUs+=2000000;p.navigationQueued(true);
    p.navigationBuilt();send(LV_EVENT_REFR_START);send(LV_EVENT_RENDER_START);clockUs+=1000;
    send(LV_EVENT_RENDER_READY);send(LV_EVENT_REFR_READY);assert(p.visibleNavigation().maxUs>=2001000);
    p.appBuild(15000);
    assert(p.appBuilds().samples==1);
    p.appBuild(2000,true);assert(p.appBuilds().samples==2);
    assert(p.coldStarts().samples==1&&p.warmStarts().samples==1&&p.warmStarts().maxUs==2000);
    send(LV_EVENT_REFR_START);send(LV_EVENT_REFR_READY);
    assert(p.noDrawRefreshes()==1);
    p.luaRead(UINT32_MAX);
    assert(p.luaReads().p99Ms==4294968);
    p.navigationFailed();clockUs+=2000000;p.updateCadence(false,false,false,0);assert(timerPeriod==100);
    p.updateCadence(false,true,false,0);assert(timerPeriod==200);
    touchCB(&touch);assert(timerPeriod==33&&timerReady>0);
    p.updateCadence(true,false,false,0);assert(timerPaused&&p.refreshPeriod()==0);
    clockUs+=2000000;p.updateCadence(false,false,false,0);assert(!timerPaused&&invalidated==1);
    animationCount=1;p.updateCadence(false,false,false,0);assert(timerPeriod==33);animationCount=0;
    p.updateCadence(false,false,true,0);assert(timerPeriod==100);
    p.urgentUpdate();p.updateCadence(false,true,false,0);assert(timerPeriod==33);
    p.updateCadence(true,false,false,0);assert(timerPaused);
    p.widgetUpdate(true);p.widgetUpdate(false);assert(p.widgetChanges()==1&&p.widgetSkips()==1);
    lv_area_t area{0,0,9,19};lv_event_t flush{LV_EVENT_FLUSH_START,eventUser,&area};eventCB(&flush);
    assert(p.flushCount()==1&&p.flushPixels()==200&&p.maxFlushPixels()==200);
    assert(p.render().meanUs>0);
}
''')
            output=t/'test'
            cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(t),
                 '-I',str(ROOT/'src/services'),str(ROOT/'src/services/UiPerformanceMonitor.cpp'),
                 str(t/'test.cpp'),'-o',str(output)]
            compiler=shutil.which('g++') or shutil.which('clang++')
            if compiler:
                cmd[0]=compiler
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists():self.skipTest('C++ compiler unavailable')
                output=t/'test.exe'
                cmd=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 /I"{t}" /I"{ROOT / "src/services"}" "{ROOT / "src/services/UiPerformanceMonitor.cpp"}" "{t / "test.cpp"}" /Fe:"{output}""'
            subprocess.run(cmd,cwd=t,check=True,capture_output=True,text=True)
            subprocess.run([str(output)],check=True,capture_output=True,text=True)

    def test_integration_points_and_retained_legacy_profile(self):
        dm=(ROOT/'src/hardware/DisplayManager.cpp').read_text()
        app=(ROOT/'src/core/AppManager.cpp').read_text()
        report=(ROOT/'src/services/DiagnosticsService.cpp').read_text()
        self.assertIn('UiPerformanceMonitor::instance().attach(',dm)
        self.assertIn('UiPerformanceMonitor::instance().navigationBuilt()',app)
        self.assertIn('lv_obj_clean(parent)',app)
        self.assertIn('lvgl_render_fps=',report)
        self.assertIn('ui_visible_navigation',report)
        # This patch must never modify BSP rotation, DMA buffers or CPU clock.
        self.assertIn('ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL',dm)
        self.assertIn('cfg.lv_adapter_cfg.task_core_id = performance::kUiCore',dm)


if __name__=='__main__':
    unittest.main()
