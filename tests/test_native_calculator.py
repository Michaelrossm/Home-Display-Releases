from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class NativeCalculatorTests(unittest.TestCase):
    def test_real_model(self):
        code='#include "CalculatorModel.h"\n'+r'''
#include <cassert>
int main(){
 hp::CalculatorModel c;
 auto press=[&](std::initializer_list<const char*> keys){for(auto k:keys)c.press(k);};
 auto check=[&](const char* text){if(c.display()!=text){std::fprintf(stderr,"Expected %s got %s\n",text,c.display().c_str());std::abort();}};
 press({"2","+","3","="});check("5");press({"="});check("8");press({"="});check("11");
 press({"7","+","3","="});check("10");
 press({"AC","0",",","1","+","0",",","2","="});check("0,3");
 press({"AC","12345678901234","+","1","="});check("12345678901235");
 press({"AC","200","+","10","%","="});check("220");
 press({"AC","1","/","6","="});check("0,16666666666667");
 press({"AC","12","x","3","="});check("36");
 press({"AC","12","x^2"});check("144");
 press({"AC","4","1/x"});check("0,25");
 press({"AC","9","sqrt"});check("3");
 press({"AC","9","+/-","sqrt"});check("Fehler");press({"4"});check("4");
 press({"AC","9","/","0","="});check("Fehler");
 press({"AC","12","DEL"});check("1");press({"DEL"});check("0");
 press({"AC","5","+/-","-","2","="});check("-7");
 press({"AC","123456789012345"});check("12345678901234");
 press({"AC","5","M+","AC","2","M+","MR"});check("7");assert(c.hasMemory());
 press({"AC","3","M-","MR"});check("4");press({"MC","MR"});check("0");assert(!c.hasMemory());
 press({"AC","8","+","-","2","="});check("6");
 for(int i=0;i<500;++i)c.press("0");assert(c.entry().size()<32);
}
'''
        test_system_performance.SystemPerformanceTests.native(self,code,[ROOT/'src/apps/calculator'])

    def test_native_registration_and_no_background_work(self):
        header=(ROOT/'src/apps/calculator/CalculatorApp.h').read_text(encoding='utf-8')
        source=(ROOT/'src/apps/calculator/CalculatorApp.cpp').read_text(encoding='utf-8')
        main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
        self.assertIn('static hp::CalculatorApp calculatorApp;',main)
        self.assertRegex(main,r'&calculatorApp\s*[,}]')
        self.assertIn('return "calculator"',header)
        self.assertIn('return "@calculator"',header)
        self.assertIn('launcherVisible() const override {return true;}',header)
        for forbidden in ('lv_timer_create','xTaskCreate','lua_','NetworkWorker'):
            self.assertNotIn(forbidden,source)
        self.assertIn('lv_obj_set_grid_dsc_array',source)
        self.assertIn('LV_EVENT_CLICKED',source)
        self.assertNotIn('lv_color_hex',source)
        for color in ('background','surface','card','accent','onAccent','textPrimary','textSecondary','border'):
            self.assertIn('theme.'+color+'()',source)
        for label in ('Wurzel','Quadrat','Kehrwert','Alles löschen','Ziffer löschen'):
            self.assertIn(label,source)
        for symbol in ('±','×','÷','−','√','x²'):
            self.assertIn(symbol,source)
        for label in ('"MC"','"MR"','"M+"','"M-"'):
            self.assertNotIn(label,source)
        icon=(ROOT/'src/ui/NativeAppIcon.h').read_text(encoding='utf-8')
        self.assertIn('Vector "1+1"',icon)

if __name__=='__main__':unittest.main()
