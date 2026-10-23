#include "WebLoginService.h"
#include "WebLoginPolicy.h"
#include "WebLoginPage.h"
#include "WebServerService.h"
#include "FileTransferService.h"
#include "WebAdminAuth.h"
#include "WebCsrfService.h"
#include "ConfigService.h"
#include "CredentialStore.h"
#include "StorageService.h"
#include "TimeService.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include <mutex>
namespace hp {
class WebLoginPersistence {public:
    static bool read(void* data,size_t size){return CredentialStore::instance().readOAuthRecord("web_sessions",data,size);}
    static bool write(const void* data,size_t size){return CredentialStore::instance().writeOAuthRecord("web_sessions",data,size);}
};
namespace {
struct Record {uint32_t magic=0x48575332;uint32_t generation=0;uint8_t password[32]{};webLogin::Session sessions[8]{};};
struct State {std::mutex mutex;Record record;bool loaded=false,writable=true;unsigned failures=0;uint32_t blockedAt=0,blockedFor=0;};
State& state(){static State value;return value;}
uint64_t sessionTick(){return uint64_t(esp_timer_get_time())/1000;}
bool secure(httpd_req_t* req){return WebServerService::instance().secureRequest(req)||FileTransferService::instance().secureRequest(req);}
void headers(httpd_req_t* req){httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(req,"X-Frame-Options","DENY");}
void wipe(String& s){if(s.length())mbedtls_platform_zeroize(const_cast<char*>(s.c_str()),s.length());s="";}
bool hash(const String& value,uint8_t out[32]){return mbedtls_sha256(reinterpret_cast<const unsigned char*>(value.c_str()),value.length(),out,0)==0;}
void load(State& s){if(s.loaded)return;s.loaded=true;Record saved{};
    if(WebLoginPersistence::read(&saved,sizeof(saved))&&saved.magic==s.record.magic){s.record=saved;for(auto& session:s.record.sessions)if(!session.remembered)session={};}
    else {auto& storage=StorageService::instance();for(const char* suffix:{"",".bak",".tmp"})if(storage.exists(StorageArea::Config,String("/credentials/web_sessions.bin")+suffix))s.writable=false;}
    mbedtls_platform_zeroize(&saved,sizeof(saved));
}
bool persist(State& s){if(!s.writable)return false;Record saved=s.record;for(auto& session:saved.sessions)if(!session.remembered)session={};const bool ok=WebLoginPersistence::write(&saved,sizeof(saved));mbedtls_platform_zeroize(&saved,sizeof(saved));return ok;}
bool passwordMatches(State& s){auto& config=ConfigService::instance();String password=config.getSecretString("web_admin_pwd","");uint8_t digest[32]{};const bool ok=password.length()&&hash(password,digest);wipe(password);const bool same=ok&&s.record.generation==config.getU32("web_auth_rev",0)&&webLogin::equal(digest,s.record.password,32);mbedtls_platform_zeroize(digest,sizeof(digest));return same;}
String cookie(httpd_req_t* req){char buffer[1024]{};if(httpd_req_get_hdr_value_str(req,"Cookie",buffer,sizeof(buffer))!=ESP_OK)return "";String result;
    char* at=buffer;while(*at){while(*at==' '||*at==';')++at;char* end=strchr(at,';');if(end)*end=0;if(!strncmp(at,"__Host-hp_session=",18)){if(result.length()){wipe(result);break;}result=at+18;}if(!end)break;at=end+1;}
    mbedtls_platform_zeroize(buffer,sizeof(buffer));if(!webLogin::token(result.c_str()))wipe(result);return result;
}
bool requestBody(httpd_req_t* req,JsonDocument& doc){char type[40]{},site[32]{},mark[8]{};
    if(!secure(req)||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json")||req->content_len<2||req->content_len>512)return false;
    if(httpd_req_get_hdr_value_str(req,"Sec-Fetch-Site",site,sizeof(site))==ESP_OK&&strcmp(site,"same-origin")&&strcmp(site,"none"))return false;
    if(req->uri&&strstr(req->uri,"/login")&&(httpd_req_get_hdr_value_str(req,"X-HP-Login",mark,sizeof(mark))!=ESP_OK||strcmp(mark,"1")))return false;
    char body[513]{};int read=0;while(read<req->content_len){const int n=httpd_req_recv(req,body+read,req->content_len-read);if(n<=0){mbedtls_platform_zeroize(body,sizeof(body));return false;}read+=n;}
    const bool ok=!deserializeJson(doc,static_cast<const char*>(body),read)&&doc.is<JsonObject>();mbedtls_platform_zeroize(body,sizeof(body));return ok;
}
struct PasswordWipe {JsonDocument& doc;~PasswordWipe(){const char* value=doc["password"].as<const char*>();if(value)mbedtls_platform_zeroize(const_cast<char*>(value),strlen(value));}};
}
bool WebLoginService::browser(httpd_req_t* req){char mode[32]{};return secure(req)&&httpd_req_get_hdr_value_str(req,"Sec-Fetch-Mode",mode,sizeof(mode))==ESP_OK;}
bool WebLoginService::authorized(httpd_req_t* req){if(!secure(req))return false;String token=cookie(req);if(!token.length())return false;uint8_t digest[32]{};const bool hashed=hash(token,digest);wipe(token);if(!hashed)return false;
    auto& s=state();std::lock_guard<std::mutex> guard(s.mutex);load(s);bool ok=false;
    if(passwordMatches(s)){const auto now=TimeService::instance().nowUtc();for(const auto& session:s.record.sessions)if(webLogin::equal(digest,session.digest,32)&&webLogin::active(session,now>0?uint64_t(now):0,sessionTick()))ok=true;}
    mbedtls_platform_zeroize(digest,sizeof(digest));return ok;
}
esp_err_t WebLoginService::deny(httpd_req_t* req){headers(req);if(req->method==HTTP_GET&&strncmp(req->uri,"/api/",5)){String target="/login?next=";for(const unsigned char* p=reinterpret_cast<const unsigned char*>(req->uri);*p;++p){char encoded[4];snprintf(encoded,sizeof(encoded),"%%%02X",*p);target+=encoded;if(target.length()>3072){target="/login";break;}}httpd_resp_set_status(req,"303 See Other");httpd_resp_set_hdr(req,"Location",target.c_str());return httpd_resp_sendstr(req,"Bitte anmelden");}httpd_resp_set_status(req,"401 Unauthorized");return httpd_resp_sendstr(req,"Bitte unter /login anmelden");}
esp_err_t WebLoginService::page(httpd_req_t* req){if(!secure(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"HTTPS required");headers(req);httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_sendstr(req,webLoginPage);}
esp_err_t WebLoginService::script(httpd_req_t* req){if(!secure(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"HTTPS required");headers(req);httpd_resp_set_type(req,"application/javascript");return httpd_resp_sendstr(req,webLoginScript);}
esp_err_t WebLoginService::login(httpd_req_t* req){headers(req);JsonDocument doc;PasswordWipe cleanup{doc};if(!requestBody(req,doc)||!doc["username"].is<const char*>()||!doc["password"].is<const char*>()||!doc["remember"].is<bool>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Anmeldung");
    String password=doc["password"].as<String>(),username=doc["username"].as<String>();const bool remember=doc["remember"];auto& s=state();std::lock_guard<std::mutex> guard(s.mutex);load(s);
    if(s.blockedFor&&uint32_t(millis()-s.blockedAt)<s.blockedFor*1000u){wipe(password);const String retry=String(s.blockedFor-uint32_t(millis()-s.blockedAt)/1000u);httpd_resp_set_status(req,"429 Too Many Requests");httpd_resp_set_hdr(req,"Retry-After",retry.c_str());return httpd_resp_sendstr(req,"Zu viele Fehlversuche. Bitte später erneut anmelden.");}
    s.blockedFor=0;
    const bool valid=username.length()<=64&&password.length()<=96&&WebAdminAuth::verify(username,password);
    if(!valid){wipe(password);s.failures=s.failures<20?s.failures+1:20;s.blockedFor=webLogin::lockSeconds(s.failures);s.blockedAt=millis();httpd_resp_set_status(req,"401 Unauthorized");return httpd_resp_sendstr(req,"Benutzername oder Passwort falsch.");}
    const auto wall=TimeService::instance().nowUtc();if(remember&&wall<1700000000){wipe(password);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Für Angemeldet bleiben zuerst die Gerätezeit synchronisieren.");}
    Record prior=s.record;String configured=ConfigService::instance().getSecretString("web_admin_pwd","");uint8_t fingerprint[32]{};
    // Bind the session to the secret actually submitted and verified, not a
    // replacement password that might have changed concurrently in Settings.
    size_t difference=password.length()^configured.length();for(size_t i=0;i<password.length();++i)difference|=password[i]^(i<configured.length()?configured[i]:0);
    const bool fingerprintOk=!difference&&configured.length()&&hash(configured,fingerprint);wipe(password);wipe(configured);if(!fingerprintOk){mbedtls_platform_zeroize(&prior,sizeof(prior));mbedtls_platform_zeroize(fingerprint,sizeof(fingerprint));return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Anmeldung nicht verfügbar oder Passwort währenddessen geändert. Erneut anmelden.");}
    const auto generation=ConfigService::instance().getU32("web_auth_rev",0);
    if(s.record.generation!=generation||!webLogin::equal(fingerprint,s.record.password,32))s.record=Record{};s.record.generation=generation;memcpy(s.record.password,fingerprint,32);mbedtls_platform_zeroize(fingerprint,sizeof(fingerprint));
    String existing=cookie(req);uint8_t old[32]{};if(existing.length()&&hash(existing,old))for(auto& session:s.record.sessions)if(webLogin::equal(old,session.digest,32))session={};wipe(existing);mbedtls_platform_zeroize(old,sizeof(old));
    unsigned index=8;for(unsigned i=0;i<8;++i)if(!webLogin::active(s.record.sessions[i],wall>0?uint64_t(wall):0,sessionTick())){index=i;break;}if(index==8){s.record=prior;mbedtls_platform_zeroize(&prior,sizeof(prior));httpd_resp_set_status(req,"429 Too Many Requests");return httpd_resp_sendstr(req,"Maximal acht Sitzungen. Auf einem anderen Gerät abmelden oder Ablauf abwarten.");}
    uint8_t random[32]{};esp_fill_random(random,sizeof(random));String token;token.reserve(64);for(auto b:random){char part[3];snprintf(part,sizeof(part),"%02x",b);token+=part;}mbedtls_platform_zeroize(random,sizeof(random));
    auto& session=s.record.sessions[index];session={};session.issued=wall>0?wall:1;session.expires=remember?session.issued+webLogin::rememberSeconds:0;session.started=sessionTick();session.remembered=remember;
    if(token.length()!=64||!hash(token,session.digest)||(remember&&!persist(s))){s.record=prior;mbedtls_platform_zeroize(&prior,sizeof(prior));wipe(token);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzung nicht gespeichert. Ohne Angemeldet bleiben versuchen.");}
    // Revoke an old remembered cookie even when replacing it with a session cookie.
    if(!remember){bool hadRemembered=false;for(const auto& entry:prior.sessions)hadRemembered|=entry.remembered!=0;if(hadRemembered&&!persist(s)){s.record=prior;wipe(token);mbedtls_platform_zeroize(&prior,sizeof(prior));return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzung konnte nicht sicher ersetzt werden.");}}
    mbedtls_platform_zeroize(&prior,sizeof(prior));s.failures=s.blockedFor=0;String header="__Host-hp_session="+token+"; Path=/; Secure; HttpOnly; SameSite=Strict";if(remember)header+="; Max-Age="+String(webLogin::rememberSeconds);
    httpd_resp_set_hdr(req,"Set-Cookie",header.c_str());wipe(token);httpd_resp_set_type(req,"application/json");const auto result=httpd_resp_sendstr(req,"{\"ok\":true}");wipe(header);return result;
}
esp_err_t WebLoginService::logout(httpd_req_t* req){headers(req);JsonDocument doc;if(!requestBody(req,doc)||!authorized(req)||!WebCsrfService::instance().validate(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Abmeldung verweigert");
    String token=cookie(req);uint8_t digest[32]{};hash(token,digest);wipe(token);auto& s=state();std::lock_guard<std::mutex> guard(s.mutex);Record prior=s.record;bool remembered=false;
    for(auto& session:s.record.sessions)if(webLogin::equal(digest,session.digest,32)){remembered|=session.remembered!=0;session={};}mbedtls_platform_zeroize(digest,sizeof(digest));
    if(remembered&&!persist(s)){s.record=prior;mbedtls_platform_zeroize(&prior,sizeof(prior));return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Abmeldung nicht gespeichert. Bitte erneut versuchen.");}mbedtls_platform_zeroize(&prior,sizeof(prior));
    httpd_resp_set_hdr(req,"Set-Cookie","__Host-hp_session=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0");return httpd_resp_sendstr(req,"Abgemeldet");
}
esp_err_t WebLoginService::csrf(httpd_req_t* req){if(!authorized(req))return deny(req);headers(req);JsonDocument doc;doc["token"]=WebCsrfService::instance().token();String body;serializeJson(doc,body);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,body.c_str());}
}
