from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

class SharedUiServicesTests(unittest.TestCase):
    def test_actual_calendar_job_bounds_cancellation_and_serialization(self):
        fixture = read('tests/native/printer_fake.h').split('\nnamespace hp {', 1)[0]
        api = read('src/apps/calendar/CalendarApi.cpp')
        runner = 'void completeCalendarJob' + api.split('void completeCalendarJob',1)[1].split('bool executeCalendarOperation',1)[0]
        fixture += r'''
namespace hp {
struct LuaJobService {
 static constexpr size_t kBodyBytes=256,kResponseBytes=128;
 struct Request {String body="{}",context="query";std::atomic<bool>* cancelled=nullptr;};
 struct Result {bool ok=false,cancelled=false;String error,body;size_t size=0;int status=0;};
};
unsigned calls=0;bool cancelDuring=false,large=false;
bool executeCalendarOperation(const char*,JsonVariantConst,JsonDocument& out,String&,const std::atomic<bool>* cancel){
 ++calls;out["title"]=large?String(200,'x'):String("Termin");
 if(cancelDuring)const_cast<std::atomic<bool>*>(cancel)->store(true);
 return true;
}
'''
        case = r'''
}
int main(){using namespace hp;LuaJobService::Request request;LuaJobService::Result result;
 executeCalendarJob(request,result);assert(result.ok&&result.status==200&&result.size==result.body.length());assert(calls==1);
 std::atomic<bool> cancel{true};request.cancelled=&cancel;result={};executeCalendarJob(request,result);assert(result.cancelled&&!result.ok&&calls==1);
 cancel=false;cancelDuring=true;result={};executeCalendarJob(request,result);assert(result.cancelled&&!result.ok&&result.body.empty()&&calls==2);
 cancelDuring=false;cancel=false;large=true;result={};executeCalendarJob(request,result);assert(!result.ok&&result.error=="calendar_result_limit");
 large=false;request.body="{";result={};auto before=calls;executeCalendarJob(request,result);assert(result.error=="calendar_json"&&calls==before);
 request.body=String(257,' ');result={};executeCalendarJob(request,result);assert(result.error=="calendar_json"&&calls==before);
 request.body="[[[[[[[[[{}]]]]]]]]]";result={};executeCalendarJob(request,result);assert(result.error=="calendar_json"&&calls==before);
}
'''
        includes = [ROOT / '.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src']
        test_system_performance.SystemPerformanceTests().native(fixture + runner + case, includes)

    def test_actual_admin_auth_credentials_fail_closed_and_refresh(self):
        source = '\n'.join(line for line in read('src/services/WebAdminAuth.cpp').splitlines()
                           if not line.startswith('#include'))
        fixture = test_system_performance.SystemPerformanceTests().string_fixture()
        fixture += r'''
constexpr int ESP_OK=0;
struct httpd_req_t {String authorization;};
namespace hp {struct WebLoginService {static bool authorized(httpd_req_t*){return false;}static bool browser(httpd_req_t*){return false;}};}
int httpd_req_get_hdr_value_str(httpd_req_t* r,const char*,char* out,size_t size){
 if(r->authorization.empty()||r->authorization.length()>=size)return -1;
 strcpy(out,r->authorization.c_str());return 0;
}
unsigned wiped=0;
void mbedtls_platform_zeroize(void* p,size_t n){memset(p,0,n);++wiped;}
int mbedtls_base64_decode(unsigned char* out,size_t cap,size_t* n,const unsigned char* src,size_t size){
 // Inject decoder results; production uses pinned mbedTLS, not this fixture.
 String s(reinterpret_cast<const char*>(src),size);String decoded;
 if(s=="DEFAULT")decoded="admin:Passwort";
 else if(s=="OLD")decoded="admin:old";
 else if(s=="NEW")decoded="admin:new";
 else if(s=="USER")decoded="guest:new";
 else if(s=="PREFIX")decoded="admin:ne";
 else if(s=="LONG")decoded="admin:new-extra";
 else return -1;
 if(decoded.length()>cap)return -1;
 *n=decoded.length();memcpy(out,decoded.data(),*n);return 0;
}
namespace hp {
struct ConfigService {
 String password;bool write=true,fail=false,migrated=false,unreadable=false;unsigned writes=0;
 static ConfigService& instance(){static ConfigService c;return c;}
 bool writable(){return write;}
 String getSecretString(const char*,const char*){return password;}
 bool hasSecret(const char*){return unreadable||!password.empty();}
 bool putSecretString(const char*,const String& value){++writes;if(fail)return false;password=value;return true;}
 bool getBool(const char*,bool){return migrated;}
 bool putBool(const char*,bool value){if(fail)return false;migrated=value;return true;}
};
}
'''
        case = r'''
int main(){using namespace hp;auto& c=ConfigService::instance();httpd_req_t req{"Basic DEFAULT"};
 c.fail=true;assert(!WebAdminAuth::authorized(&req));assert(c.password.empty());
 c.fail=false;c.unreadable=true;assert(!WebAdminAuth::authorized(&req));c.unreadable=false;
 c.write=false;assert(!WebAdminAuth::authorized(&req));
 c.write=true;assert(WebAdminAuth::initialize());assert(c.migrated);assert(WebAdminAuth::authorized(&req));
 c.password="old";req.authorization="Basic OLD";assert(WebAdminAuth::authorized(&req));
 auto writes=c.writes;assert(WebAdminAuth::initialize());assert(c.writes==writes);
 c.password="new";assert(!WebAdminAuth::authorized(&req));req.authorization="Basic NEW";assert(WebAdminAuth::authorized(&req));
 c.write=false;assert(WebAdminAuth::initialize());assert(WebAdminAuth::authorized(&req));
 for(auto s:{"Basic USER","Basic PREFIX","Basic LONG","Basic INVALID","Bearer NEW",""}){req.authorization=s;assert(!WebAdminAuth::authorized(&req));}
 req.authorization=String(200,'x');assert(!WebAdminAuth::authorized(&req));assert(wiped>20);
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture + source + case)

    def test_all_servers_share_auth_without_plaintext_cache(self):
        for file in ['WebServerService.cpp','FileTransferService.cpp','RecoveryWebService.cpp']:
            source = read('src/services/' + file)
            self.assertIn('WebAdminAuth::authorized(req)', source)
            self.assertNotIn('mbedtls_base64_decode', source)
            self.assertNotIn('String webAdminPassword', source)
        for file in ['WebServerService.cpp','FileTransferService.cpp']:
            source = read('src/services/' + file)
            self.assertIn('Cross-origin administration denied', source)
            self.assertIn('WebCsrfService::instance().validate(req)', source)
        auth = read('src/services/WebAdminAuth.cpp')
        self.assertNotIn('NetworkWorker', auth)
        self.assertNotIn('xTaskCreate', auth)
        validator = read('tools/release_validator.py')
        self.assertIn("rel == 'src/services/WebAdminAuth.cpp'", validator)

    def test_theme_selection_shared_on_display_and_web(self):
        controls = read('src/ui/ThemeControls.cpp')
        widgets = read('src/ui/SettingsWidgets.h')
        web = read('src/services/WebServerService.cpp')
        self.assertIn('catalog.optionList()', controls)
        self.assertIn('catalog.refreshSd()', controls)
        self.assertIn('ThemeCatalog::accentName(id)', controls)
        self.assertNotIn('Dunkel\\nAMOLED\\nHell', controls)
        self.assertIn('ThemeControls::bindSelection(mode, false)', widgets)
        self.assertIn('ThemeControls::bindSelection(accent, true)', widgets)
        self.assertNotIn('nativeThemeChanged', widgets)
        self.assertIn('ThemeControls::instance().refreshControlCenter()', read('src/ui/UiShell.cpp'))
        self.assertIn('s.themes.map(t=>new Option(t.name,t.id))', web)
        self.assertIn('id < catalog.count()', web)
        self.assertIn('ThemeCatalog::accentName(id)', web)
        self.assertIn('m>ThemeCatalog::kMaxMode', web)
        handler = web.split('esp_err_t WebServerService::themeHandler',1)[1].split('esp_err_t WebServerService::brightnessHandler',1)[0]
        self.assertIn('DisplayUiGuard guard', handler)
        self.assertIn('writeAuthorized(req)', handler)

    def test_calendar_common_runner_preserves_frontend_authorization(self):
        api = read('src/apps/calendar/CalendarApi.cpp')
        lua = read('src/apps/lua/CalendarLuaBridge.cpp')
        native = read('src/apps/calendar/CalendarController.cpp')
        web = read('src/services/WebServerService.cpp')
        self.assertIn('job.handler=executeCalendarJob', lua)
        self.assertIn('executeCalendarJob(request, result)', native)
        self.assertIn('executeCalendarJob(request,result)', web)
        self.assertIn('job.required=perm', lua)
        self.assertIn('BackgroundExecuting', lua)
        self.assertIn('SecurityWriteGuard security', web)
        self.assertIn('settingsAllowed()', web)
        self.assertIn('NestingLimit(8)', api)
        self.assertIn('kBodyBytes', api)
        self.assertIn('kResponseBytes', api)
        self.assertIn('result.cancelled=true', api)
        for forbidden in ['lv_obj_', 'xTaskCreate']:
            self.assertNotIn(forbidden, api)

if __name__ == '__main__':
    unittest.main()
