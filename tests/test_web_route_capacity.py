from pathlib import Path
import re
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]


class WebRouteCapacityTests(unittest.TestCase):
    def test_actual_start_registers_every_route_and_cleans_up_failures(self):
        web = (ROOT / 'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        begin = 'bool WebServerService::begin()' + web.split('bool WebServerService::begin()', 1)[1].split('size_t WebServerService::activeClients()', 1)[0]
        oauth = (ROOT / 'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        oauth = 'const httpd_uri_t* oauthWebRoutes(' + oauth.split('const httpd_uri_t* oauthWebRoutes(', 1)[1]
        oauth = oauth.rsplit('\n}', 1)[0]
        names = re.findall(r'\{"[^"\n]+",\s*HTTP_\w+,\s*(\w+)(?:,|\})', begin)
        transfer = re.findall(r'FileTransferService::(\w+)', begin)
        login = re.findall(r'WebLoginService::(\w+)', begin)
        account = re.findall(r'HTTP_\w+,\s*(\w+),', oauth)
        expected = len(re.findall(r'\{"/[^"\n]*",\s*HTTP_', begin + oauth))-1 # HTTP redirect has its own table.
        self.assertGreater(expected, 72, 'Exercise the original exhausted-table regression')
        string_fixture = test_system_performance.SystemPerformanceTests().string_fixture().replace('String(unsigned n)', 'String(unsigned long value):std::string(std::to_string(value)){}\n    String(unsigned n)')
        fixture = string_fixture + r'''
#include <limits>
#include <set>
using esp_err_t=int;
constexpr int ESP_OK=0,HTTP_GET=0,HTTP_POST=1,tskNO_AFFINITY=-1;
struct httpd_req_t{const char* uri="/";};
struct sockaddr{};struct sockaddr_in {int sin_family=2,sin_addr=0;};using socklen_t=unsigned;
constexpr int INET_ADDRSTRLEN=16,AF_INET=2,HTTPD_503_SERVICE_UNAVAILABLE=503;
int httpd_req_to_sockfd(httpd_req_t*){return 0;}
int getsockname(int,sockaddr*,socklen_t*){return -1;}
const char* inet_ntop(int,const void*,char*,unsigned){return nullptr;}
struct NetworkService {static NetworkService& instance(){static NetworkService s;return s;}String ip(){return "192.168.178.118";}};
struct httpd_uri_t {const char* uri;int method;int(*handler)(httpd_req_t*);void* context;};
struct Server {unsigned capacity=0;std::vector<httpd_uri_t> routes;};
using httpd_handle_t=Server*;
struct httpd_config_t {uint16_t max_uri_handlers=8;unsigned server_port=0,ctrl_port=0,max_open_sockets=0,stack_size=0,recv_wait_timeout=0,send_wait_timeout=0;int core_id=-1;bool lru_purge_enable=false;void* uri_match_fn=nullptr;};
httpd_config_t HTTPD_DEFAULT_CONFIG(){return {};}
void* httpd_uri_match_wildcard=reinterpret_cast<void*>(1);
unsigned starts=0,stops=0,failRoute=0;bool failStart=false;
int httpd_start(Server** server,const httpd_config_t* cfg){++starts;if(failStart)return 10;*server=new Server;(*server)->capacity=cfg->max_uri_handlers;return 0;}
int httpd_register_uri_handler(Server* server,const httpd_uri_t* route){
 if(server->routes.size()>=server->capacity||server->routes.size()+1==failRoute)return 11;
 for(const auto& existing:server->routes)if(existing.method==route->method&&!strcmp(existing.uri,route->uri))return 12;
 server->routes.push_back(*route);return 0;
}
int httpd_stop(Server* server){++stops;delete server;return 0;}
struct httpd_ssl_config_t {httpd_config_t httpd;const unsigned char* servercert=nullptr;size_t servercert_len=0;const unsigned char* prvtkey_pem=nullptr;size_t prvtkey_len=0;unsigned port_secure=0,tls_handshake_timeout_ms=0;};
httpd_ssl_config_t HTTPD_SSL_CONFIG_DEFAULT(){return {};}
int httpd_ssl_start(Server** server,const httpd_ssl_config_t* cfg){assert(cfg->port_secure==443);assert(cfg->httpd.max_open_sockets==6);assert(cfg->httpd.lru_purge_enable);assert(cfg->httpd.recv_wait_timeout==5&&cfg->httpd.send_wait_timeout==5);return httpd_start(server,&cfg->httpd);}
int httpd_resp_set_status(httpd_req_t*,const char*){return 0;}
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*){return 0;}
int httpd_resp_sendstr(httpd_req_t*,const char*){return 0;}
constexpr int HTTPD_400_BAD_REQUEST=400;
int httpd_resp_send_err(httpd_req_t*,int,const char*){return 0;}
const char* esp_err_to_name(int error){return error==10?"start_failed":"route_failed";}
namespace hp {
namespace performance {constexpr int kIoCore=0;}
struct EventLog {std::vector<String> messages;static EventLog& instance(){static EventLog log;return log;}void add(const char*,const char* tag,const String& text){messages.push_back(String(tag)+":"+text);}};
String csrf;
struct OAuthProtocol {static String randomToken(){return String(std::string(64,'a'));}};
struct WebTlsIdentity {static WebTlsIdentity& instance(){static WebTlsIdentity identity;return identity;}bool load(){return true;}const unsigned char* certificate(){return nullptr;}const unsigned char* key(){return nullptr;}size_t certificateSize(){return 1;}size_t keySize(){return 1;}};
struct SettingsService {static SettingsService& instance(){static SettingsService s;return s;}struct Snapshot{String hostname="homepanel";};Snapshot snapshot(){return {};}};
'''
        fixture += ''.join('int ' + name + '(httpd_req_t*){return 0;}\n' for name in sorted(set(account)))
        fixture += oauth
        fixture += 'struct WebLoginService {' + ''.join('static int ' + name + '(httpd_req_t*){return 0;}' for name in sorted(set(login))) + '};\n'
        fixture += 'struct FileTransferService {' + ''.join('static int ' + name + '(httpd_req_t*){return 0;}' for name in sorted(set(transfer))) + '};\n'
        fixture += 'struct WebServerService {static constexpr size_t socketCapacity(){return 6;}Server* server_=nullptr;Server* redirect_=nullptr;bool begin();void stop(){if(redirect_){httpd_stop(redirect_);redirect_=nullptr;}if(server_){httpd_stop(server_);server_=nullptr;}}'
        fixture += ''.join('static int ' + name + '(httpd_req_t*){return 0;}' for name in sorted(set(names) | {'appPageHandler'})) + '};\n'
        fixture += begin + '\n}\n'
        fixture += r'''
int main(){using namespace hp;WebServerService service;assert(service.begin());
 assert(service.server_->routes.size()==EXPECTED);
 assert(service.server_->capacity==EXPECTED);
#if !HOMEPANEL_FEATURE_INTERCOM
 for(const auto& route:service.server_->routes)assert(std::string(route.uri).find("intercom")==std::string::npos);
#endif
 assert(std::string(service.server_->routes.back().uri)=="/apps/*");
 size_t wildcards=0;for(const auto& route:service.server_->routes)if(std::string(route.uri)=="/apps/*")++wildcards;assert(wildcards==1);
 const auto started=starts;for(unsigned i=0;i<100;++i)assert(service.begin());assert(starts==started);
 service.stop();assert(!service.server_);
 failStart=true;assert(!service.begin()&&!service.server_);failStart=false;
 failRoute=1;const auto stopped=stops;assert(!service.begin()&&!service.server_&&stops==stopped+1);
 failRoute=60;assert(!service.begin()&&!service.server_); // OAuth registration also cleans up.
 failRoute=EXPECTED;assert(!service.begin()&&!service.server_); // Wildcard failure.
 failRoute=0;assert(service.begin());service.stop();
 bool startLog=false,routeLog=false;for(const auto& message:EventLog::instance().messages){startLog|=message.find("WEB80_START_FAILED")!=String::npos;routeLog|=message.find("WEB80_ROUTE_FAILED")!=String::npos;}
 assert(startLog&&routeLog);
}
'''
        guarded=re.findall(r'#if HOMEPANEL_FEATURE_INTERCOM(.*?)#endif',begin,re.S)
        gated_count=sum(len(re.findall(r'\{"/[^"\n]*",\s*HTTP_',block)) for block in guarded)
        self.assertEqual(gated_count,5)
        for enabled in (0,1):
            count=expected if enabled else expected-gated_count
            test_system_performance.SystemPerformanceTests().native(
                '#define HOMEPANEL_FEATURE_INTERCOM '+str(enabled)+'\n'+fixture.replace('EXPECTED',str(count)))

    def test_capacity_and_registration_share_the_oauth_table(self):
        source = (ROOT / 'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        begin = source.split('bool WebServerService::begin()', 1)[1].split('size_t WebServerService::activeClients()', 1)[0]
        self.assertIn('oauthWebRoutes(oauthCount)', begin)
        self.assertIn('sizeof(routes)/sizeof(routes[0])', begin)
        self.assertIn('sizeof(serviceRoutes)/sizeof(serviceRoutes[0])', begin)
        self.assertIn('oauthCount+1', begin)
        self.assertLess(begin.index('cfg.max_uri_handlers='), begin.index('httpd_ssl_start('))
        self.assertEqual(begin.count('"/apps/*"'), 1)
        self.assertNotIn('AppRegistry', begin)
        self.assertNotIn('xTaskCreate', begin)


if __name__ == '__main__':
    unittest.main()
