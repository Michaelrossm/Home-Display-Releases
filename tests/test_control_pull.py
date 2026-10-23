from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

class ControlPullTests(unittest.TestCase):
    def test_animation_completion_uses_target_not_deferred_layout(self):
        source=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        callback=source.split('void UiShell::controlAnimationDone',1)[1].split('void UiShell::settleControlCenter',1)[0]
        code=r'''
#include <cassert>
struct lv_obj_t{int coordinates=-12,y=0;bool hidden=false;};
struct lv_anim_t{void* var;int end_value;};
constexpr unsigned LV_OBJ_FLAG_HIDDEN=1;
int lv_display_get_vertical_resolution(void*){return 800;}
int lv_obj_get_y(lv_obj_t* o){return o->coordinates;}
void lv_obj_set_y(lv_obj_t* o,int y){o->y=y;} // LVGL layout is deferred.
void lv_obj_add_flag(lv_obj_t* o,unsigned){o->hidden=true;}
void lv_obj_remove_flag(lv_obj_t* o,unsigned){o->hidden=false;}
struct UiShell{static void controlAnimationDone(lv_anim_t*);};
void UiShell::controlAnimationDone'''+callback+r'''
int main(){lv_obj_t panel;lv_anim_t open{&panel,0};UiShell::controlAnimationDone(&open);
assert(!panel.hidden&&panel.y==0); // Last rendered frame can still be negative.
panel.coordinates=0;lv_anim_t close{&panel,-800};UiShell::controlAnimationDone(&close);assert(panel.hidden&&panel.y==-800);
panel.coordinates=-800;UiShell::controlAnimationDone(&open);assert(!panel.hidden&&panel.y==0);
open.var=nullptr;UiShell::controlAnimationDone(&open);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_real_gesture_policy(self):
        code = r'''
#include "ControlPull.h"
#include <cassert>
int main(){hp::ControlPull p;
 p.begin(600,0,0,800,0);assert(!p.move(600,8,50));assert(!p.finish(200));
 p.begin(600,0,0,800,0);assert(p.move(600,420,500));assert(p.amount==420);assert(p.finish(800));
 p.begin(600,0,0,800,0);assert(p.move(600,300,500));assert(!p.finish(800));
 p.begin(600,0,0,800,0);p.move(600,500,500);p.move(600,480,510);assert(p.finish(520));
 p.begin(600,760,800,800,0);p.move(600,250,500);p.move(600,270,510);assert(!p.finish(520));
 p.begin(600,0,0,800,0);assert(p.move(600,120,500));assert(!p.finish(800));
 p.begin(600,0,0,800,0);assert(p.move(600,80,80));p.move(600,80,90);assert(p.finish(90));
 p.begin(600,0,0,800,0);p.move(600,79,79);assert(!p.finish(90));
 p.begin(600,0,0,800,0);p.move(600,80,80);assert(!p.finish(260));
 p.begin(600,0,0,800,0);p.move(600,80,1000);assert(!p.finish(1001));
 p.begin(600,0,0,800,0);p.move(600,400,1000);assert(p.finish(1500));
 p.begin(600,760,800,800,0);p.move(600,681,79);assert(p.finish(90));
 p.begin(600,760,800,800,0);assert(p.move(600,350,500));assert(!p.finish(800));
 p.begin(600,760,800,800,0);assert(p.move(600,400,500));assert(p.finish(800));
 p.begin(600,760,800,800,0);assert(p.move(600,710,500));assert(p.finish(800));
 p.begin(600,760,800,800,0);assert(p.move(600,680,80));assert(!p.finish(90));
 p.begin(600,0,0,800,0);assert(!p.move(660,10,80));assert(!p.move(660,400,500));assert(!p.finish(800));
 p.begin(600,0,0,800,0);assert(p.move(600,900,500));assert(p.amount==800);
 p.begin(600,760,800,800,0);assert(p.move(600,-100,500));assert(p.amount==0);
 p.begin(600,0,400,800,0);assert(!p.move(660,5,50));assert(p.finish(200));
 p.begin(600,0,0,800,0xfffffff0u);assert(p.move(600,80,64));assert(p.finish(80));
}'''
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);(work/'check.cpp').write_text(code,encoding='utf-8')
            shutil.copy(ROOT/'src/ui/ControlPull.h',work)
            if shutil.which('g++'):
                command=['g++','-std=c++17','check.cpp','-o','check.exe']
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists(): self.skipTest('No native C++ compiler')
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result=subprocess.run(command,cwd=work,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(work/'check.exe')],cwd=work,check=True,timeout=10)

    def test_shell_uses_fixed_edge_and_existing_lvgl_owner(self):
        source=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        self.assertIn('lv_obj_set_size(edge,LV_PCT(100),24)',source)
        self.assertIn('LV_OBJ_FLAG_PRESS_LOCK',source)
        self.assertIn('controlPull_.move',source)
        self.assertIn('controlPull_.finish',source)
        self.assertIn('lv_anim_set_completed_cb',source)
        self.assertNotIn('controlGesture',source)
        self.assertNotIn('lv_timer_handler(',source)
        self.assertIn('SecurityService::instance().tabletLocked()',source)
        self.assertIn('IdleStage::Off',source)
