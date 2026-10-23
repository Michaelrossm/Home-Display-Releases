from pathlib import Path
import unittest
import test_system_performance
from test_native_phonebook import function
ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
class PostalLookupTests(unittest.TestCase):
    def test_actual_native_apply_preserves_manual_fields_and_rejects_stale_address(self):
        apply=function(read('src/apps/phonebook/PhonebookApp.cpp'),'void applyPostal(')
        code=r'''
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <cassert>
using String=std::string;
struct lv_obj_t{String text;};
const char* lv_textarea_get_text(lv_obj_t* o){return o->text.c_str();}
void lv_textarea_set_text(lv_obj_t* o,const char* s){o->text=s;}
void lv_label_set_text(lv_obj_t* o,const char* s){o->text=s;}
struct PhonebookPage{
enum class View{Contacts,ContactEditor};View view=View::ContactEditor;
struct AddressInputs{lv_obj_t* fields[8]{};};std::vector<AddressInputs> addressInputs;
size_t postalAddress=0;String postalCode="93495",postalCountry="DE",postalBefore[3];JsonDocument postalResult;lv_obj_t* status;
'''+apply+r'''
};
int main(){PhonebookPage p;lv_obj_t fields[8],status;p.status=&status;
PhonebookPage::AddressInputs row;for(int i=0;i<8;++i)row.fields[i]=&fields[i];p.addressInputs.push_back(row);
fields[3].text="93495";fields[5].text="Meine Region";fields[6].text="DE";p.postalBefore[1]="Meine Region";p.postalBefore[2]="DE";
deserializeJson(p.postalResult,R"({"places":[{"city":"Weiding","region":"Bayern","country":"Deutschland"}]})");
p.applyPostal(0);assert(fields[4].text=="Weiding"&&fields[5].text=="Meine Region"&&fields[6].text=="Deutschland");
fields[4].text="Manueller Ort";fields[6].text="DE";p.applyPostal(0);assert(fields[4].text=="Manueller Ort");
fields[4].text="";fields[3].text="80331";p.applyPostal(0);assert(fields[4].text.empty());
fields[3].text="93495";p.view=PhonebookPage::View::Contacts;p.applyPostal(0);assert(fields[4].text.empty());
}
'''
        test_system_performance.SystemPerformanceTests().native(code,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_shared_consumers_permissions_and_stale_native_results(self):
        bridge=read('src/apps/lua/LuaNetworkBridge.cpp')
        self.assertIn('lua_register(L,"hp_postal_lookup",postalLookup)',bridge)
        self.assertIn('app->allows(AppPermission::NetworkInternet)',bridge.split('int postalLookup',1)[1])
        self.assertIn('r.owner=app->id()',bridge.split('int postalLookup',1)[1])
        self.assertIn('r.handler=executePostalLookupJob',bridge)
        self.assertIn('Lane::Transfer',bridge.split('int postalLookup',1)[1])
        self.assertIn('lookupPostalCode(',read('src/apps/calendar/CalendarApi.cpp'))
        self.assertIn('executePostalLookupJob(request,result)',read('src/services/PhonebookWebApi.cpp'))
        native=read('src/apps/phonebook/PhonebookApp.cpp')
        self.assertIn('request.handler=executePostalLookupJob',native)
        self.assertIn('postalCode!=lv_textarea_get_text',native)
        self.assertIn('postalBefore[i]==lv_textarea_get_text',native)
        self.assertIn('cancelPostal();if(dismissKeyboard)',native)
        self.assertNotIn('esp_http_client',native)
        self.assertNotIn('api.zippopotam.us',bridge)
        providers=[p.relative_to(ROOT).as_posix() for p in (ROOT/'src').rglob('*') if p.suffix in ('.cpp','.h') and 'https://api.zippopotam.us/' in p.read_text(encoding='utf-8')]
        self.assertEqual(providers,['src/services/PostalLookupService.cpp'])
        self.assertIn('inputs.postalCode.value!==postal',read('src/services/PhonebookWebPage.h'))

    def test_real_worker_transport_validation_bounds_and_cancel(self):
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]
        fixture=fixture.replace('size_t write(unsigned char c)', 'bool concat(const char* s,size_t n){append(s,n);return true;}\nsize_t write(unsigned char c)')
        fixture+='\n#include <cmath>\n#include <cstdlib>\n'
        fixture+=r'''
constexpr int ESP_OK=0,ESP_FAIL=-1,HTTP_EVENT_ON_DATA=1;
using esp_err_t=int;
struct esp_http_client_event_t{void* user_data;int event_id,data_len;void* data;};
struct esp_http_client_config_t{const char* url;void* crt_bundle_attach;int timeout_ms;bool disable_auto_redirect;esp_err_t(*event_handler)(esp_http_client_event_t*);void* user_data;};
void* esp_crt_bundle_attach=nullptr;esp_http_client_config_t cfg;int status=200,calls=0;bool failInit=false;
String response=R"({"places":[{"place name":"München","state":"Bayern"},{"place name":"Zweiter Ort","state":"Bayern"}]})";
void* esp_http_client_init(const esp_http_client_config_t* c){cfg=*c;assert(c->disable_auto_redirect&&c->timeout_ms==4000);return failInit?nullptr:&cfg;}
int esp_http_client_perform(void*){++calls;esp_http_client_event_t e{cfg.user_data,1,int(response.length()),const_cast<char*>(response.c_str())};return cfg.event_handler(&e);}
int esp_http_client_get_status_code(void*){return status;}void esp_http_client_cleanup(void*){}
namespace hp {
struct RegionService{static RegionService& instance(){static RegionService r;return r;}const char* country(){return "DE";}static int find(const char*){return -1;}static const char* code(size_t){return "DE";}};
struct NetworkWorker{bool worker=true;static NetworkWorker& instance(){static NetworkWorker n;return n;}bool isWorkerContext(){return worker;}};
struct NetworkService{bool connected=true;static NetworkService& instance(){static NetworkService n;return n;}bool online(){return connected;}};
struct TimeService{bool synced=true;static TimeService& instance(){static TimeService n;return n;}bool valid(){return synced;}};
}
'''
        source='\n'.join(l for l in read('src/services/PostalLookupService.cpp').splitlines() if not l.startswith('#include'))
        fixture+=source+r'''
int main(){using namespace hp;JsonDocument a,out;String error;std::atomic<bool> cancel{false};a["postal_code"]="80331";
 assert(lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));assert(out["places"].size()==2&&out["places"][0]["city"]=="München");
 a["postal_code"]="80/31";assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&calls==1);
 a["postal_code"]="80331";cancel=true;assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&calls==1);cancel=false;
 NetworkService::instance().connected=false;assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&calls==1);NetworkService::instance().connected=true;
 a["country"]="FR";a["postal_code"]="75001";response=R"({"country":"France","places":[{"place name":"Paris","state":"Ile-de-France","latitude":"48.86","longitude":"2.34"}]})";
 assert(lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&out["places"][0]["latitude"].as<double>()==48.86&&out["places"][0]["country"]=="France");
 a["country"]="GB";a["postal_code"]="SW1A 1AA";assert(lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));
 a["country"]="../";assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));a["country"]="DE";a["postal_code"]="01067";
 assert(lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&out["postal_code"]=="01067");
 status=404;assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));status=200;
 response=String(8193,'x');assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));
 response=R"({"places":[{"place name":"City"}]})";assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel));
 NetworkWorker::instance().worker=false;assert(!lookupPostalCode(a.as<JsonVariantConst>(),out,error,&cancel)&&error=="worker_required");
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
    def test_ui_has_no_blocking_http_and_rejects_stale_results(self):
        web=read('src/services/CalendarWebPage.h');native=read('src/apps/calendar/CalendarApp.cpp')
        for item in ('postalGeneration','draft!==eventDraft','Mehrere Orte','job(\'postal_lookup\''):
            self.assertIn(item,web)
        self.assertIn('controller.action("postal_lookup"',native)
        self.assertIn('postalCode==lv_textarea_get_text',native)
        self.assertNotIn('HTTPClient',native)
        self.assertNotIn("new Option('Alle Benutzer'",web)
