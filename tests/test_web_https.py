from pathlib import Path
import unittest
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]
class WebHttpsTests(unittest.TestCase):
 def test_actual_account_parser_owns_strings_after_secret_scratch_wipe(self):
  source=(ROOT/'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
  parser='bool postBody('+source.split('bool postBody(',1)[1].split('EmailProvider provider(',1)[0]
  fixture=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
#include <ArduinoJson.h>
constexpr int ESP_OK=0;
String csrf(std::string(64,'a'));
struct OAuthProtocol{static bool constantEqual(const char* a,const String& b){return b==a;}};
struct httpd_req_t{std::string payload;int content_len=0;size_t cursor=0;bool validToken=true;};
int httpd_req_get_hdr_value_str(httpd_req_t* r,const char* name,char* out,size_t n){std::string value=std::string(name)=="Content-Type"?"application/json":std::string(64,r->validToken?'a':'b');if(value.size()>=n)return -1;strcpy(out,value.c_str());return 0;}
int httpd_req_recv(httpd_req_t* r,char* out,size_t n){n=std::min(n,r->payload.size()-r->cursor);memcpy(out,r->payload.data()+r->cursor,n);r->cursor+=n;return int(n);}
unsigned wipes=0;void mbedtls_platform_zeroize(void* p,size_t n){memset(p,0,n);++wipes;}
'''+parser+r'''
int main(){httpd_req_t req;req.payload=R"({"slot":0,"username":"person@example.com","password":"aaaa-bbbb-cccc-dddd","serverUrl":"https://example.com/dav/"})";req.content_len=req.payload.size();JsonDocument doc;
 assert(postBody(&req,doc));assert(wipes==1);
 assert(std::string(doc["username"].as<const char*>())=="person@example.com");
 assert(std::string(doc["password"].as<const char*>())=="aaaa-bbbb-cccc-dddd");
 req.cursor=0;req.validToken=false;assert(!postBody(&req,doc)&&req.cursor==0);
 req.validToken=true;req.content_len=1537;assert(!postBody(&req,doc)&&req.cursor==0);
}
'''
  test_system_performance.SystemPerformanceTests.native(self,fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
 def test_password_never_accepted_on_http(self):
  s=(ROOT/'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
  action=s.split('esp_err_t action(',1)[1].split('esp_err_t portal(',1)[0]
  self.assertLess(action.index('secureRequest(req)'),action.index('postBody(req,doc)'))
  self.assertIn('SecretWipe',action)
  self.assertIn('doc["password"]|""',action)
 def test_secret_job_is_bounded_zeroized_and_existing_worker(self):
  h=(ROOT/'src/services/CalendarSyncService.h').read_text(encoding='utf-8')
  s=(ROOT/'src/services/CloudAccountApi.cpp').read_text(encoding='utf-8')
  self.assertIn('char password[193]',h)
  self.assertIn('~DavConfigJob()',h)
  self.assertIn('strnlen(password,193)',s)
  self.assertIn('submit(runDavConfig',s)
  self.assertIn('job->user,job->password',s)
 def test_identity_not_shared_or_stored_plaintext(self):
  s=(ROOT/'src/services/WebTlsIdentity.cpp').read_text(encoding='utf-8')
  self.assertIn('esp_fill_random',s)
  self.assertIn('writeOAuthRecord("web_tls"',s)
  self.assertIn('mbedtls_platform_zeroize',s)
  self.assertNotIn('BEGIN PRIVATE KEY',s)
 def test_both_web_services_use_tls(self):
  for file in ('WebServerService.cpp','FileTransferService.cpp'):
   s=(ROOT/'src/services'/file).read_text(encoding='utf-8')
   self.assertIn('httpd_ssl_start(',s)
   self.assertIn('httpd_ssl_stop(',s)
  s=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
  self.assertIn('redirectCfg.max_uri_handlers=1',s)
  self.assertIn('"/*",HTTP_GET',s)
