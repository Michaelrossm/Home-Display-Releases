from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def stripped(path):return '\n'.join(l for l in read(path).splitlines() if not l.startswith('#include') and l!='#pragma once')

class RegionTests(unittest.TestCase):
    def test_actual_catalog_async_persistence_default_and_no_ui_nvs_reads(self):
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]
        fixture=fixture.replace('size_t length()const', 'bool equalsIgnoreCase(const char* s)const{String a=*this,b=s;a.toLowerCase();b.toLowerCase();return a==b;}\nsize_t length()const')
        fixture+=r'''
namespace hp {
struct ConfigService {String saved="DE";unsigned reads=0,writes=0;bool fail=false;static ConfigService& instance(){static ConfigService c;return c;}
 String getString(const char*,const char*){++reads;return saved;}bool putString(const char* key,const char* value){assert(strlen(key)<=15);++writes;if(fail)return false;saved=value;return true;}};
struct SecurityWriteGuard{};
struct SecurityService {bool allowed=true,locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};
struct NetworkWorker {bool reject=false;void(*fn)(void*)=nullptr;void* context=nullptr;static NetworkWorker& instance(){static NetworkWorker w;return w;}
 bool submitLocalTransfer(void(*f)(void*),void* p,const char*){if(reject)return false;fn=f;context=p;return true;}void run(){auto f=fn;fn=nullptr;assert(f);f(context);}};
}
'''+stripped('src/services/RegionService.h')+'\n'+stripped('src/services/RegionService.cpp')+r'''
int main(){using namespace hp;auto& r=RegionService::instance();auto& cfg=ConfigService::instance();auto& worker=NetworkWorker::instance();r.begin();assert(!strcmp(r.country(),"DE"));unsigned reads=cfg.reads;
for(int i=0;i<1000;++i)assert(!strcmp(r.country(),"DE"));assert(cfg.reads==reads);
for(size_t i=0;i<r.count();++i){assert(strlen(r.code(i))==2&&r.find(r.code(i))==int(i));for(size_t j=0;j<i;++j)assert(strcmp(r.code(i),r.code(j)));}
assert(r.find("France")==r.find("FR")&&r.find("Germany")==0&&r.find("Great Britain")==r.find("GB"));
int fr=r.find("FR"),ch=r.find("CH");assert(r.request(fr)&&r.busy()&&!r.request(ch));assert(!strcmp(r.country(),"DE"));worker.run();assert(!r.busy()&&!strcmp(r.country(),"FR")&&cfg.saved=="FR");
cfg.fail=true;assert(r.request(ch));worker.run();assert(!strcmp(r.country(),"FR")&&cfg.saved=="FR");cfg.fail=false;
SecurityService::instance().allowed=false;assert(!r.request(ch));SecurityService::instance().allowed=true;worker.reject=true;assert(!r.request(ch)&&!r.busy());worker.reject=false;
unsigned writes=cfg.writes;assert(r.request(fr)&&cfg.writes==writes&&!r.busy());
JsonDocument doc;assert(!deserializeJson(doc,r.catalogJson()));assert(doc["country"]=="FR"&&doc["countries"].size()==r.count());
cfg.saved="ZZ";r.begin();assert(!strcmp(r.country(),"DE"));
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_all_frontends_use_catalog_and_model_preserving_picker(self):
        self.assertIn('RegionService::instance().begin()',read('src/main.cpp'))
        self.assertIn('RegionService::instance().country()',read('src/services/PostalLookupService.cpp'))
        self.assertIn('countryPicker(right,p.address[5])',read('src/apps/calendar/CalendarApp.cpp'))
        self.assertIn('countryPicker(cell,row.fields[i+1])',read('src/apps/phonebook/PhonebookApp.cpp'))
        self.assertIn('RegionService::instance().request',read('src/apps/settings/SettingsApp.cpp'))
        self.assertIn('hpWeb.countrySelect',read('src/services/CalendarWebPage.h'))
        self.assertIn('hpWeb.countrySelect',read('src/services/PhonebookWebPage.h'))
        self.assertNotIn("value.trim()||'DE'",read('src/services/CalendarWebPage.h'))
        self.assertNotIn('String postal,country="DE"',read('src/apps/lua/LuaNetworkBridge.cpp'))
        self.assertIn('region_country',read('src/services/RegionService.cpp'))
        self.assertNotIn('xTaskCreate',read('src/services/RegionService.cpp'))

if __name__=='__main__':unittest.main()
