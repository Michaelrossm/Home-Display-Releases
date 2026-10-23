from pathlib import Path
import unittest
import test_system_performance
from test_native_phonebook import function
ROOT=Path(__file__).resolve().parents[1]
class PhonebookLifecycleNamesTests(unittest.TestCase):
    def test_actual_mdns_unicast_requests(self):
        source=(ROOT/'src/services/MinimalMdns.cpp').read_text(encoding='utf-8')
        method=function(source,'void MinimalMdns::handlePacket(')
        code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#define strcasecmp _stricmp
struct sockaddr_in{uint16_t sin_port;};
constexpr uint16_t P=5353,ANY=255;uint16_t htons(uint16_t p){return p;}
uint16_t r16(const uint8_t* p){return (p[0]<<8)|p[1];}
bool readName(const uint8_t*,size_t,size_t& off,char* out,size_t){strcpy(out,"home-display.local");off=16;return true;}
struct MinimalMdns {char fqdn_[64]="home-display.local";bool answered=false,unicast=false;uint16_t id=0;
bool sendResponse(const sockaddr_in* to,uint16_t value){answered=true;unicast=to!=nullptr;id=value;return true;}
void handlePacket(const uint8_t*,size_t,const sockaddr_in&);};
'''+method+r'''
int main(){MinimalMdns m;sockaddr_in from{5353};uint8_t packet[20]{};packet[5]=1;packet[17]=1;packet[19]=1;
m.handlePacket(packet,20,from);assert(m.answered&&!m.unicast);
packet[18]=128;m.handlePacket(packet,20,from);assert(m.unicast&&m.id==0);
packet[18]=0;from.sin_port=9999;m.handlePacket(packet,20,from);assert(m.unicast);
packet[1]=9;m.handlePacket(packet,20,from);assert(m.id==9);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        self.assertIn('m.imr_interface.s_addr=inet_addr(ip_.toString().c_str())',source)
        self.assertIn('IP_MULTICAST_IF',source)

    def test_actual_person_options_preserve_structured_names(self):
        options=(ROOT/'src/apps/calendar/CalendarPersonOptions.h').read_text(encoding='utf-8')
        options='\n'.join(line for line in options.splitlines() if not line.startswith('#'))
        fixture=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
#include <ArduinoJson.h>
#include <cctype>
#include <cstring>
namespace hp {struct ConfigService {String saved="{}";bool fail=false;
static ConfigService& instance(){static ConfigService c;return c;}
String getString(const char*,const char*){return saved;}
bool putString(const char*,const String& value){if(fail)return false;saved=value;return true;}};}
'''+options+r'''
int main(){JsonDocument doc;doc["names"]["00000001"]["first"]="Anna Maria";doc["names"]["00000001"]["last"]="von Beispiel";
assert(hp::calendar::savePersonOptions(doc));JsonDocument restored;assert(hp::calendar::loadPersonOptions(restored));
assert(std::string(restored["names"]["00000001"]["first"].as<const char*>())=="Anna Maria");
assert(std::string(restored["names"]["00000001"]["last"].as<const char*>())=="von Beispiel");
hp::ConfigService::instance().fail=true;doc["names"]["00000001"]["first"]="Other";assert(!hp::calendar::savePersonOptions(doc));assert(hp::calendar::loadPersonOptions(restored));
hp::ConfigService::instance().saved="{}";assert(hp::calendar::loadPersonOptions(restored));
hp::ConfigService::instance().saved=R"({"names":{"wrong":{"first":"A","last":"B"}}})";assert(!hp::calendar::loadPersonOptions(restored));
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_actual_delete_only_destroys_root(self):
        source=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        deleted=function(source,'static void deleted(')
        code=r'''
#include <cstdlib>
#include <new>
#include <cassert>
unsigned destroys=0,timers=0,frees=0,postalCancels=0;
struct lv_obj_t{};struct lv_event_t{void* user;lv_obj_t* target;};
void* lv_event_get_user_data(lv_event_t* e){return e->user;}
lv_obj_t* lv_event_get_target(lv_event_t* e){return e->target;}
void lv_timer_delete(void*){++timers;}
void heap_caps_free(void* p){++frees;free(p);}
struct PhonebookPage;
struct App{PhonebookPage* page_;unsigned generation_=5;};
struct PhonebookPage {lv_obj_t* root;void* timer;App* app;~PhonebookPage(){++destroys;}void cancelPostal(){++postalCancels;}
'''+deleted+r'''
};
int main(){lv_obj_t root,child;App app;auto* p=new(malloc(sizeof(PhonebookPage))) PhonebookPage{&root,&root,&app};app.page_=p;
lv_event_t childEvent{p,&child};PhonebookPage::deleted(&childEvent);
assert(!destroys&&!timers&&!frees&&!postalCancels&&app.page_==p&&app.generation_==5);
lv_event_t rootEvent{p,&root};PhonebookPage::deleted(&rootEvent);
assert(destroys==1&&timers==1&&frees==1&&postalCancels==1&&!app.page_&&app.generation_==6);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_actual_structured_vcard_round_trip(self):
        source=(ROOT/'src/services/ContactProtocol.cpp').as_posix()
        code='#include <cassert>\n#include "'+source+'"\n'+r'''
int main(){hp::ContactProtocol::Card card;card.name="Anna Maria von Beispiel";card.firstName="Anna Maria";card.lastName="von Beispiel";
std::string text;assert(hp::ContactProtocol::serializeVcard(card,text));
assert(text.find("N:von Beispiel;Anna Maria;;;")!=std::string::npos);
hp::ContactProtocol::Card copy;assert(hp::ContactProtocol::parseVcard(text,copy));
assert(copy.firstName==card.firstName&&copy.lastName==card.lastName&&copy.name==card.name);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_ip_redirect_does_not_depend_on_mdns_or_host_header(self):
        source=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        redirect=source.split('const httpd_uri_t redirectRoute=')[1].split('},nullptr};')[0]
        self.assertIn('getsockname(httpd_req_to_sockfd(req)',redirect)
        self.assertIn('String target=String("https://")+host',redirect)
        self.assertNotIn('get_hdr_value',redirect)
        self.assertNotIn('.local',redirect)

    def test_person_fields_share_existing_storage(self):
        for path in ('src/services/PhonebookWebPage.h','src/services/CalendarWebPage.h',
                     'src/apps/phonebook/PhonebookApp.cpp','src/apps/calendar/CalendarApp.cpp'):
            text=(ROOT/path).read_text(encoding='utf-8')
            self.assertIn('Vorname',text);self.assertIn('Nachname',text)
        source=(ROOT/'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        self.assertIn('options["names"][key]["first"]',source)
        self.assertIn('legacyPersonName(c.name,c.firstName,c.lastName)',source)

if __name__=='__main__':unittest.main()
