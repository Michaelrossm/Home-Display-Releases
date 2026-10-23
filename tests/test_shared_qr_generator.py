from pathlib import Path
import unittest
import test_system_performance
from test_native_phonebook import function

ROOT=Path(__file__).resolve().parents[1]
class SharedQrTests(unittest.TestCase):
    def test_actual_canvas_stride_and_quiet_zone(self):
        source=(ROOT/'src/services/QrGenerator.cpp').read_text(encoding='utf-8')
        code=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
#include <algorithm>
#include <cstring>
#include <cassert>
#define LV_COLOR_FORMAT_I1 1
#define LV_OPA_COVER 255
struct Header {int cf=1,w=84,h=84,stride=12;};
struct Draw {Header header;uint8_t data[1016]{};};
struct lv_obj_t {Draw draw;bool invalidated=false;};
Draw* lv_canvas_get_draw_buf(lv_obj_t* o){return &o->draw;}
void mbedtls_platform_zeroize(void* p,size_t n){memset(p,0,n);}
void lv_draw_buf_clear(Draw* d,void*){memset(d->data,0,sizeof(d->data));}
int lv_color_white(){return 0;}int lv_color_black(){return 1;}
int lv_color_to_32(int c,int){return c;}
void lv_canvas_set_palette(lv_obj_t*,int,int){}
void lv_image_cache_drop(Draw*){}
void lv_obj_invalidate(lv_obj_t* o){o->invalidated=true;}
struct QrGenerator {
static bool matrix(const String&,int& size,String& bits){size=21;bits=std::string(111,'0').c_str();bits[0]='8';return true;}
static bool update(lv_obj_t*,const String&);
};
'''+function(source,'bool QrGenerator::update(')+r'''
int main(){lv_obj_t qr;assert(QrGenerator::update(&qr,"test"));assert(qr.invalidated);
for(int y=0;y<84;y++)for(int x=0;x<84;x++){
bool black=(qr.draw.data[8+y*12+x/8]&(0x80>>(x%8)))!=0;
assert(black==(x>=21&&x<23&&y>=21&&y<23));}
qr.draw.header.w=30;qr.invalidated=false;assert(!QrGenerator::update(&qr,"test"));assert(!qr.invalidated);
qr.draw.header.cf=2;assert(!QrGenerator::update(&qr,"test"));
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_actual_wifi_escaping_and_bounds(self):
        source=(ROOT/'src/services/QrGenerator.cpp').read_text(encoding='utf-8')
        code=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
#include <cassert>
struct QrGenerator {static String wifi(const String&,const String&);};
'''+function(source,'String escapeWifi(')+function(source,'String QrGenerator::wifi(')+r'''
int main(){
assert(QrGenerator::wifi("Guest","")=="WIFI:T:nopass;S:Guest;P:;;");
assert(QrGenerator::wifi("A;B:C","p,\\\"")=="WIFI:T:WPA;S:A\\;B\\:C;P:p\\,\\\\\\\";;");
assert(QrGenerator::wifi("", "secret").length()==0);
assert(QrGenerator::wifi(String(std::string(33,'a').c_str()),"secret").length()==0);
assert(QrGenerator::wifi("Guest",String(std::string(65,'a').c_str())).length()==0);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_one_encoder_for_every_consumer(self):
        callers=['src/ui/EmailAccountsUi.cpp','src/services/OAuthWebRoutes.cpp',
                 'src/ui/QrDialog.cpp','src/apps/settings/SettingsApp.cpp']
        for path in callers:
            text=(ROOT/path).read_text(encoding='utf-8')
            self.assertIn('QrGenerator::',text)
            self.assertNotIn('lv_qrcode_update(',text)
            self.assertNotIn('qrcodegen_encodeText(',text)
        source=(ROOT/'src/services/QrGenerator.cpp').read_text(encoding='utf-8')
        self.assertEqual(source.count('qrcodegen_encodeText('),1)
        self.assertIn('matrix(payload,size,bits)',source)
        self.assertNotIn('xTaskCreate',source)

    def test_saved_wifi_guard_and_contact_vcard(self):
        settings=(ROOT/'src/apps/settings/SettingsApp.cpp').read_text(encoding='utf-8')
        callback=function(settings,'void SettingsApp::wifiQrCb(')
        self.assertLess(callback.index('settingsAllowed()'),callback.index('getWifiProfile('))
        self.assertIn('kMaxWifiProfiles',settings)
        self.assertIn('wifiProfileSsid(slot)',settings)
        contacts=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        self.assertIn('exportVcard(p.contact,card)',contacts)
        self.assertIn('QrDialog::show(p.contact.name.c_str(),card,false,p.root)',contacts)

    def test_sensitive_dialog_expiration_and_wipe(self):
        source=(ROOT/'src/ui/QrDialog.cpp').read_text(encoding='utf-8')
        for expected in ('settingsAllowed()', 'tabletLocked()', '>=60000',
                         'LV_EVENT_DELETE', 'mbedtls_platform_zeroize', 'lv_timer_delete'):
            self.assertIn(expected,source)

if __name__=='__main__':unittest.main()
