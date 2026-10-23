from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]


class LuaUiDimensionTests(unittest.TestCase):
    def test_actual_dimension_policy_preserves_pinned_lvgl_tags(self):
        source=(ROOT/'src/apps/lua/LuaBasicUiBridge.cpp').read_text(encoding='utf-8')
        function='int32_t uiDimension'+source.split('int32_t uiDimension',1)[1].split('int bridgeSetSize',1)[0]
        header=(ROOT/'managed_components/lvgl__lvgl/src/misc/lv_area.h').read_text(encoding='utf-8')
        macros='\n'.join(line for line in header.splitlines() if line.startswith('#define LV_COORD_') or line.startswith('#define LV_PCT') or line.startswith('#define LV_SIZE_CONTENT'))
        test_system_performance.SystemPerformanceTests().native('#include <cstdint>\n#include <cassert>\n#define LV_MAX(a,b) ((a)>(b)?(a):(b))\n#define LV_MIN(a,b) ((a)<(b)?(a):(b))\n'+macros+'\n'+function+r'''
int main(){
 for(int p=0;p<=100;++p)assert(uiDimension(LV_PCT(p))==LV_PCT(p));
 assert(uiDimension(LV_SIZE_CONTENT)==LV_SIZE_CONTENT);
 assert(uiDimension(48)==48&&uiDimension(1280)==1280&&uiDimension(4096)==4096);
 assert(uiDimension(0)==1&&uiDimension(-20)==1);
 assert(uiDimension(INT64_MAX)==4096&&uiDimension(INT64_MIN)==1);
 assert(uiDimension(LV_PCT(200))==4096);
}
''')
        self.assertIn('uiDimension(luaL_optinteger(L, 2, 100))',source)
        self.assertIn('uiDimension(luaL_optinteger(L, 3, 50))',source)


if __name__=='__main__':unittest.main()
