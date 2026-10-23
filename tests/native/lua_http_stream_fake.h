#include <utility>
const int portMAX_DELAY=1;int xSemaphoreTake(void*,int){return 1;}void xSemaphoreGive(void*){}
const int ESP_OK=0;
enum esp_http_client_method_t{HTTP_METHOD_GET,HTTP_METHOD_POST,HTTP_METHOD_PUT,HTTP_METHOD_DELETE};
void esp_crt_bundle_attach(){}
struct esp_http_client_config_t{const char* url=nullptr;uint32_t timeout_ms=0;bool disable_auto_redirect=false;
 void(*crt_bundle_attach)()=nullptr;const char* common_name=nullptr;};
struct HttpFixture {int status=200;String response,upload;int64_t length=-1;size_t cursor=0;bool complete=true,connect=true;
 std::atomic<bool>* cancellation=nullptr;uint32_t readDelay=0;unsigned requests=0;} http;
using esp_http_client_handle_t=HttpFixture*;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* cfg){assert(cfg->disable_auto_redirect);++http.requests;http.cursor=0;http.upload="";return &http;}
void esp_http_client_set_method(esp_http_client_handle_t,esp_http_client_method_t){}
int esp_http_client_set_timeout_ms(esp_http_client_handle_t,int timeout){assert(timeout>0&&timeout<=5000);return ESP_OK;}
void esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*){}
int esp_http_client_open(esp_http_client_handle_t,size_t){return http.connect?ESP_OK:-1;}
int esp_http_client_write(esp_http_client_handle_t,const char* bytes,size_t size){http.upload.concat(bytes,size);return int(size);}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t){return http.length;}
int esp_http_client_get_status_code(esp_http_client_handle_t){return http.status;}
int esp_http_client_read(esp_http_client_handle_t,char* bytes,size_t capacity){now+=http.readDelay;
 const size_t n=std::min(capacity,http.response.size()-http.cursor);memcpy(bytes,http.response.c_str()+http.cursor,n);http.cursor+=n;
 if(http.cancellation&&n)http.cancellation->store(true);return int(n);}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t){return http.complete;}
void esp_http_client_close(esp_http_client_handle_t){}void esp_http_client_cleanup(esp_http_client_handle_t){}
int mbedtls_base64_encode(unsigned char*,size_t,size_t*,const unsigned char*,size_t){return -1;}
namespace hp {
struct ConfigService{uint32_t version=7;static ConfigService& instance(){static ConfigService s;return s;}uint32_t revision()const{return version;}};
enum class MediaCommand:uint8_t{VolumeDown=7};
struct MediaSessionService{static MediaSessionService& instance(){static MediaSessionService s;return s;}bool command(MediaCommand){return false;}};
struct EnergyHistoryService{static EnergyHistoryService& instance(){static EnergyHistoryService s;return s;}String query(const String&,int){return {};}};
struct StorageResult{bool ok=false;String value,error;size_t size=0;};
StorageResult executeLuaStorage(const String&,uint8_t,const String&,const String&){return {};}
String executeLuaDiscovery(const String&){return {};}
String luaSecretForNative(const String&,const String&){return {};}
unsigned completions=0;void publishLuaEvent(const char*,const char*,uint32_t){++completions;}
bool target(const LuaJobService::Request&,String& pinned,String& hostname){pinned="http://192.168.1.2/file";hostname="192.168.1.2";return true;}
LuaJobService& LuaJobService::instance(){static LuaJobService service;return service;}
}
