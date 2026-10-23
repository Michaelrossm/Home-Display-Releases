from pathlib import Path
import unittest
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]
class WebLoginRedirectTests(unittest.TestCase):
    def test_actual_auth_guard_preserves_location_with_idf_header_limit(self):
        web=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        guard='void commonHeaders('+web.split('void commonHeaders(',1)[1].split('bool writeAuthorized(',1)[0]
        login=(ROOT/'src/services/WebLoginService.cpp').read_text(encoding='utf-8')
        deny=next(line for line in login.splitlines() if line.startswith('esp_err_t WebLoginService::deny('))
        headers=next(line for line in login.splitlines() if line.startswith('void headers('))
        fixture=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
using esp_err_t=int;constexpr int ESP_OK=0,HTTP_GET=0,HTTP_POST=1;
struct httpd_req_t {const char* uri="/";int method=HTTP_GET;String site,status,body;std::vector<std::pair<String,String>> headers;};
int httpd_req_get_hdr_value_str(httpd_req_t* r,const char*,char* out,size_t cap){if(r->site.empty()||r->site.length()>=cap)return -1;strcpy(out,r->site.c_str());return 0;}
// Match ESP-IDF 5.5.5: repeated header names consume separate slots.
int httpd_resp_set_hdr(httpd_req_t* r,const char* name,const char* value){if(r->headers.size()==8)return -1;r->headers.emplace_back(name,value);return 0;}
int httpd_resp_set_status(httpd_req_t* r,const char* s){r->status=s;return 0;}
int httpd_resp_sendstr(httpd_req_t* r,const char* s){r->body=s;return 0;}
namespace hp {namespace WebAdminAuth{bool accept=false;bool authorized(httpd_req_t*){return accept;}}
struct WebLoginService{static int deny(httpd_req_t*);};
'''+headers+'\n'+deny+'\n'+guard+r'''
}
int main(){using namespace hp;
 for(const char* path:{"/","/calendar","/phonebook","/apps/printer_panel/","/calendar?connect=icloud"}){
  httpd_req_t r;r.uri=path;assert(!authorized(&r));assert(r.status=="303 See Other");
  assert(r.headers.size()==5);unsigned locations=0;
  for(const auto& header:r.headers)if(header.first=="Location"){++locations;assert(header.second.find("/login?next=%2F")==0);}
  assert(locations==1);if(std::string(path)=="/")assert(r.headers.back().second=="/login?next=%2F");
 }
 httpd_req_t api;api.uri="/api/status";assert(!authorized(&api)&&api.status=="401 Unauthorized"&&api.headers.size()==4);
 httpd_req_t cross;cross.site="cross-site";assert(!authorized(&cross)&&cross.status=="403 Forbidden"&&cross.headers.size()==4);
 WebAdminAuth::accept=true;httpd_req_t accepted;assert(authorized(&accepted)&&accepted.headers.empty());
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture)
