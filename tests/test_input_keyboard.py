from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InputKeyboardTests(unittest.TestCase):
    def test_native_input_policy(self):
        code = r'''
#include "InputMode.h"
#include <cassert>
int main() {
 using namespace hp::keyboardInput;
 Mode m=Mode::Text;
 for(auto mode:{Mode::Integer,Mode::Decimal,Mode::SignedDecimal,Mode::Phone,Mode::DateTime})
   assert(fromAccepted(accepted(mode))==mode);
 assert(fromAccepted(nullptr)==Mode::Text);
 assert(fromAccepted("")==Mode::Text);
 assert(fromAccepted("abc0123456789")==Mode::Text);
 assert(fromAccepted("abcdef0123456789")==Mode::Text);
 assert(fromAccepted("0123456789-")==Mode::DateTime);
 assert(fromAccepted("0123456789:")==Mode::DateTime);
 assert(parse("integer",m)&&m==Mode::Integer);
 assert(parse("number",m)&&m==Mode::Decimal);
 assert(parse("decimal",m)&&m==Mode::Decimal);
 assert(parse("signed_decimal",m)&&m==Mode::SignedDecimal);
 assert(parse("phone",m)&&m==Mode::Phone);
 assert(parse("datetime",m)&&m==Mode::DateTime);
 assert(parse("text",m)&&m==Mode::Text);
 assert(parse(nullptr,m)&&m==Mode::Text);
 assert(!parse("arbitrary",m));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / 'check.cpp').write_text('#include <initializer_list>\n' + code, encoding='utf-8')
            shutil.copy(ROOT / 'src/ui/InputMode.h', work)
            if shutil.which('g++'):
                command = ['g++', '-std=c++17', 'check.cpp', '-o', 'check.exe']
            else:
                setup = Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists():
                    self.skipTest('No native C++ compiler')
                command = f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(work / 'check.exe')], cwd=work, check=True, timeout=10)

    def test_lua_api_is_optional_and_guarded(self):
        source = (ROOT / 'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('luaL_optstring(L,5,"text")', source)
        self.assertIn('InputKeyboard::setMode(ta,mode)', source)
        self.assertIn('InputKeyboard::bind(kb, ta)', source)
        for name in ('hp_set_input_type', 'hp_ui_input_type'):
            self.assertIn(f'lua_register(L, "{name}", bridgeInputType)', source)
            self.assertIn(f'guard(L,"{name}",true)', source)

    def test_native_consumers_share_binding(self):
        for app in ('calendar/CalendarApp', 'phonebook/PhonebookApp',
                    'intercom/IntercomApp', 'photos/PhotoApp', 'settings/SettingsApp'):
            source = (ROOT / f'src/apps/{app}.cpp').read_text(encoding='utf-8')
            self.assertIn('InputKeyboard::bind(', source, app)
        self.assertIn('Mode::Phone', (ROOT / 'src/ui/AccountDataUi.cpp').read_text(encoding='utf-8'))

    def test_keymaps_are_local_and_have_exit_and_edit_keys(self):
        source = (ROOT / 'src/ui/InputKeyboard.cpp').read_text(encoding='utf-8')
        self.assertNotIn('lv_keyboard_set_map(', source)
        self.assertIn('lv_buttonmatrix_set_map(keyboard,map)', source)
        self.assertIn('lv_buttonmatrix_set_ctrl_map(keyboard,controls)', source)
        for name in ('integerMap', 'decimalMap', 'signedMap', 'phoneMap', 'dateMap'):
            body = source.split(f'{name}[]=', 1)[1].split(';', 1)[0]
            for key in ('LV_SYMBOL_BACKSPACE', 'LV_SYMBOL_CLOSE', 'LV_SYMBOL_OK'):
                self.assertIn(key, body)
        self.assertIn('LV_KEYBOARD_MODE_TEXT_LOWER', source)
        self.assertNotIn('lv_timer_create(', source)
        self.assertNotIn('xTaskCreate', source)

    def test_outside_touch_uses_existing_input_device_not_app_bubbling(self):
        source=(ROOT/'src/ui/InputKeyboard.cpp').read_text(encoding='utf-8')
        self.assertIn('lv_indev_add_event_cb(indev,outsidePress,LV_EVENT_PRESSED',source)
        self.assertIn('lv_event_get_param(event)',source)
        self.assertIn('o==keyboard||lv_obj_check_type(o,&lv_textarea_class)',source)
        self.assertIn('lv_obj_is_visible(keyboard)',source)
        self.assertIn('LV_EVENT_CANCEL',source)
        self.assertIn('keyboardDeleted,LV_EVENT_DELETE',source)
        self.assertNotIn('LV_OBJ_FLAG_EVENT_BUBBLE',source)


if __name__ == '__main__':
    unittest.main()
