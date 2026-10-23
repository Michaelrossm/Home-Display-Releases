#include "CalendarAuthService.h"
#include "ConfigService.h"
#include <esp_random.h>
#include <mbedtls/sha256.h>
namespace hp {
CalendarAuthService& CalendarAuthService::instance(){static CalendarAuthService a;return a;}
static String keyFor(uint32_t id){char b[12];snprintf(b,sizeof(b),"c%08lx",(unsigned long)id);return String(b);}
void CalendarAuthService::derive(const char*password,const uint8_t salt[16],uint8_t out[32]){uint8_t state[32]{};mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);mbedtls_sha256_update(&c,salt,16);if(password)mbedtls_sha256_update(&c,(const unsigned char*)password,strlen(password));mbedtls_sha256_finish(&c,state);mbedtls_sha256_free(&c);for(uint16_t i=0;i<4095;++i){mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);mbedtls_sha256_update(&c,state,sizeof(state));mbedtls_sha256_update(&c,salt,16);mbedtls_sha256_finish(&c,state);mbedtls_sha256_free(&c);}memcpy(out,state,32);}
bool CalendarAuthService::save(const Credential&c){auto&p=ConfigService::instance().calendarSecrets();String k=keyFor(c.personId);return p.putBytes(k.c_str(),&c,sizeof(c))==sizeof(c);}
bool CalendarAuthService::load(uint32_t id,Credential&out){auto&p=ConfigService::instance().calendarSecrets();String k=keyFor(id);return p.getBytesLength(k.c_str())==sizeof(out)&&p.getBytes(k.c_str(),&out,sizeof(out))==sizeof(out)&&out.personId==id;}
bool CalendarAuthService::setPassword(uint32_t id,const char*pw){if(!id||!pw||strlen(pw)<8||strlen(pw)>96)return false;Credential c{};c.personId=id;for(size_t i=0;i<sizeof(c.salt);i+=4){uint32_t x=esp_random();memcpy(c.salt+i,&x,min<size_t>(4,sizeof(c.salt)-i));}derive(pw,c.salt,c.hash);return save(c);}
bool CalendarAuthService::verify(uint32_t id,const char*pw){Credential c{};if(!load(id,c)||!pw)return false;uint8_t test[32];derive(pw,c.salt,test);uint8_t diff=0;for(size_t i=0;i<32;++i)diff|=test[i]^c.hash[i];return diff==0;}
}
