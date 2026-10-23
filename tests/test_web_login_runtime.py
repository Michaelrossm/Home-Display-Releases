from pathlib import Path
import unittest
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]
class WebLoginRuntimeTests(unittest.TestCase):
    def test_actual_login_logout_expiry_and_persistent_sessions(self):
        fixture=(ROOT/'tests/native/printer_fake.h').read_text(encoding='utf-8').split('\nnamespace hp {',1)[0]
        fixture+=r'''
#include <mutex>
#include "WebLoginPolicy.h"
#include "WebLoginPage.h"
using esp_err_t=int;
constexpr int ESP_OK=0,HTTP_GET=0,HTTP_POST=1,HTTPD_400_BAD_REQUEST=400,HTTPD_403_FORBIDDEN=403,HTTPD_500_INTERNAL_SERVER_ERROR=500;
struct httpd_req_t {const char* uri="/api/auth/login";int method=HTTP_POST,content_len=0;void* handle=reinterpret_cast<void*>(1);String body,response,status;size_t consumed=0;std::map<String,String> headers,out;};
int httpd_req_get_hdr_value_str(httpd_req_t* req,const char* name,char* buffer,size_t size){auto it=req->headers.find(name);if(it==req->headers.end()||it->second.length()>=size)return -1;strcpy(buffer,it->second.c_str());return 0;}
int httpd_req_recv(httpd_req_t* req,char* buffer,size_t size){const auto count=std::min(size,req->body.length()-req->consumed);memcpy(buffer,req->body.c_str()+req->consumed,count);req->consumed+=count;return int(count);}
int httpd_resp_set_hdr(httpd_req_t* req,const char* name,const char* value){req->out[name]=value;return 0;}
int httpd_resp_set_status(httpd_req_t* req,const char* value){req->status=value;return 0;}
int httpd_resp_set_type(httpd_req_t*,const char*){return 0;}
int httpd_resp_sendstr(httpd_req_t* req,const char* text){req->response=text;return 0;}
int httpd_resp_send_err(httpd_req_t* req,int code,const char* text){req->status=String(code);req->response=text;return 0;}
void mbedtls_platform_zeroize(void* p,size_t n){memset(p,0,n);}
// Deterministic transport/crypto fixture, not production cryptography.
int mbedtls_sha256(const unsigned char* p,size_t n,uint8_t* out,int){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;for(unsigned i=0;i<32;++i)out[i]=uint8_t((h>>(i%4*8))+i);return 0;}
int64_t esp_timer_get_time(){return int64_t(now)*1000;}
void esp_fill_random(void* p,size_t n){static unsigned serial=1;auto* out=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)out[i]=uint8_t(serial+i);++serial;}
namespace hp {
class WebLoginService {public:static bool authorized(httpd_req_t*);static bool browser(httpd_req_t*);static int deny(httpd_req_t*);static int page(httpd_req_t*);static int script(httpd_req_t*);static int login(httpd_req_t*);static int logout(httpd_req_t*);static int csrf(httpd_req_t*);};
struct ConfigService {String password="secret";uint32_t generation=1;static ConfigService& instance(){static ConfigService s;return s;}String getSecretString(const char*,const char*){return password;}uint32_t getU32(const char*,uint32_t){return generation;}};
namespace WebAdminAuth {bool changeDuringVerify=false;bool verify(const String& name,const String& password){auto& c=ConfigService::instance();const bool result=name=="admin"&&password==c.password;if(changeDuringVerify){c.password="replacement";++c.generation;}return result;}}
struct CredentialStore {std::vector<uint8_t> bytes;bool writable=true;static CredentialStore& instance(){static CredentialStore s;return s;}bool readOAuthRecord(const char*,void* p,size_t n){if(bytes.size()!=n)return false;memcpy(p,bytes.data(),n);return true;}bool writeOAuthRecord(const char*,const void* p,size_t n){if(!writable)return false;auto* b=static_cast<const uint8_t*>(p);bytes.assign(b,b+n);return true;}};
enum class StorageArea{Config};struct StorageService {static StorageService& instance(){static StorageService s;return s;}bool exists(StorageArea,const String&){return false;}};
struct TimeService {int64_t wall=1700000100;static TimeService& instance(){static TimeService s;return s;}int64_t nowUtc(){return wall;}};
struct WebServerService {static WebServerService& instance(){static WebServerService s;return s;}bool secureRequest(httpd_req_t* r){return r->handle==reinterpret_cast<void*>(1);}};
struct FileTransferService {static FileTransferService& instance(){static FileTransferService s;return s;}bool secureRequest(httpd_req_t* r){return r->handle==reinterpret_cast<void*>(2);}};
struct WebCsrfService {static WebCsrfService& instance(){static WebCsrfService s;return s;}String token(){return "csrf";}bool validate(httpd_req_t* r){return r->headers["X-HP-CSRF"]=="csrf";}};
}
'''
        source='\n'.join(line for line in (ROOT/'src/services/WebLoginService.cpp').read_text(encoding='utf-8').splitlines() if not line.startswith('#include'))
        fixture+=source+r'''
httpd_req_t login(bool remember=true,const char* password="secret"){httpd_req_t r;JsonDocument a;a["username"]="admin";a["password"]=password;a["remember"]=remember;serializeJson(a,r.body);r.content_len=int(r.body.length());r.headers["Content-Type"]="application/json";r.headers["X-HP-Login"]="1";r.headers["Sec-Fetch-Site"]="same-origin";return r;}
String savedCookie(const httpd_req_t& req){return req.out.at("Set-Cookie").substr(0,82);}
int main(){using namespace hp;
 auto r=login();r.headers["X-HP-Login"]="";WebLoginService::login(&r);assert(r.status=="400"&&r.out.count("Set-Cookie")==0);
 r=login();r.handle=nullptr;WebLoginService::login(&r);assert(r.status=="400");
 WebAdminAuth::changeDuringVerify=true;r=login();WebLoginService::login(&r);assert(r.status=="500"&&r.out.count("Set-Cookie")==0);WebAdminAuth::changeDuringVerify=false;ConfigService::instance().password="secret";
 r=login();WebLoginService::login(&r);assert(r.response=="{\"ok\":true}");String cookie=savedCookie(r);assert(cookie.length()==82);
 r.headers["Cookie"]=cookie;assert(WebLoginService::authorized(&r));
 // Simulate a restart: only persistent, hashed sessions are restored.
 state().record=Record{};state().loaded=false;assert(WebLoginService::authorized(&r));
 auto &config=ConfigService::instance();++config.generation;assert(!WebLoginService::authorized(&r));
 ++config.generation;assert(!WebLoginService::authorized(&r)); // Old password restored still cannot resurrect cookie.
 r=login();WebLoginService::login(&r);cookie=savedCookie(r);r.headers["Cookie"]=cookie;assert(WebLoginService::authorized(&r));
 r.uri="/api/auth/logout";r.body="{}";r.content_len=2;r.consumed=0;r.headers["X-HP-CSRF"]="bad";WebLoginService::logout(&r);assert(r.status=="403"&&WebLoginService::authorized(&r));
 r.headers["X-HP-CSRF"]="csrf";r.consumed=0;CredentialStore::instance().writable=false;WebLoginService::logout(&r);assert(r.status=="500"&&WebLoginService::authorized(&r));CredentialStore::instance().writable=true;
 r.consumed=0;WebLoginService::logout(&r);assert(!WebLoginService::authorized(&r));
 state().record=Record{};state().loaded=false;assert(!WebLoginService::authorized(&r)); // Revocation survives restart.
 CredentialStore::instance().writable=false;r=login();WebLoginService::login(&r);assert(r.status=="500"&&r.out.count("Set-Cookie")==0);CredentialStore::instance().writable=true;
 r=login(false);WebLoginService::login(&r);r.headers["Cookie"]=savedCookie(r);assert(WebLoginService::authorized(&r));now+=webLogin::sessionSeconds*1000;assert(!WebLoginService::authorized(&r));
 TimeService::instance().wall=0;r=login();WebLoginService::login(&r);assert(r.status=="409 Conflict");TimeService::instance().wall=1700000100;
 for(unsigned i=0;i<8;++i){r=login();WebLoginService::login(&r);assert(r.response=="{\"ok\":true}");}
 auto cookie8=savedCookie(r);r=login();WebLoginService::login(&r);assert(r.status=="429 Too Many Requests");r=login();r.headers["Cookie"]=cookie8;WebLoginService::login(&r);assert(r.response=="{\"ok\":true}");
 for(unsigned i=0;i<5;++i){r=login(false,"wrong");WebLoginService::login(&r);assert(r.status=="401 Unauthorized");}r=login(false);WebLoginService::login(&r);assert(r.status=="429 Too Many Requests");
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'src/services',ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
