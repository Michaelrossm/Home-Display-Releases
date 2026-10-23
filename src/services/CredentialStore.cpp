#include "CredentialStore.h"
#include "ConfigService.h"
#include "StorageService.h"
#include <memory>
#include <new>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
namespace hp {
CredentialStore& CredentialStore::instance(){ static CredentialStore c; return c; }
String CredentialStore::key(uint32_t id) const {
    char b[16]; snprintf(b,sizeof(b),"nas%08lx",(unsigned long)id); return String(b);
}
bool CredentialStore::putNasPassword(uint32_t id,const String&password){
    return id && password.length()<=192 && ConfigService::instance().putSecretString(key(id).c_str(),password);
}
String CredentialStore::nasPassword(uint32_t id){
    return ConfigService::instance().getSecretString(key(id).c_str(),"");
}
bool CredentialStore::removeNasPassword(uint32_t id){
    return ConfigService::instance().removeSecret(key(id).c_str());
}
namespace {
// One mutex covers key creation and account record access. No key is derived
// from public MAC addresses and no key is regenerated over existing records.
struct EmailCredentialGuard {
    SemaphoreHandle_t mutex;
    bool locked;
    EmailCredentialGuard() {
        static SemaphoreHandle_t shared = xSemaphoreCreateMutex();
        mutex = shared; locked = mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
    }
    ~EmailCredentialGuard() { if (locked) xSemaphoreGive(mutex); }
};
String emailRecordKey(uint8_t slot) { return String("email_") + slot; }
}
bool CredentialStore::emailKey(uint8_t out[32], bool create) {
    auto& config = ConfigService::instance();
    String hex = config.getSecretString("email_key");
    if (!hex.length() && create) {
        for (uint8_t slot = 0; slot < 5; ++slot) if (config.hasSecret(emailRecordKey(slot).c_str())) return false;
        auto& storage=StorageService::instance();
        for(const char* id:{"oauth_google","oauth_gdev","oauth_ms","oauth_0","oauth_1","oauth_2","oauth_3","oauth_4","dav_0","dav_1","dav_2","dav_3","dav_4"})
            for(const char* suffix:{".bin",".bin.bak",".bin.tmp"})
                if(storage.exists(StorageArea::Config,String("/credentials/")+id+suffix))return false;
        esp_fill_random(out, 32);
        char encoded[65];
        for (size_t i = 0; i < 32; ++i) snprintf(encoded + i * 2, 3, "%02x", out[i]);
        const bool ok = config.putSecretString("email_key", encoded);
        mbedtls_platform_zeroize(encoded, sizeof(encoded));
        return ok;
    }
    bool ok = hex.length() == 64;
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
    if (ok) for (size_t i = 0; i < 32; ++i) {
        const int a = digit(hex[i * 2]), b = digit(hex[i * 2 + 1]);
        if (a < 0 || b < 0) { ok = false; break; } out[i] = uint8_t((a << 4) | b);
    }
    if (hex.length()) mbedtls_platform_zeroize(&hex[0], hex.length());
    return ok;
}
bool CredentialStore::writeEmailRecord(uint8_t slot, const void* data, size_t size) {
    if (slot >= 5 || !data || !size || size > 512) return false;
    EmailCredentialGuard lock; if (!lock.locked) return false;
    uint8_t keyBytes[32]{}, envelope[1 + 12 + 16 + 512]{};
    char encoded[725]{}; size_t encodedSize = 0;
    envelope[0] = 1; esp_fill_random(envelope + 1, 12);
    const uint8_t aad[] = {1, slot};
    mbedtls_gcm_context context; mbedtls_gcm_init(&context);
    bool ok = emailKey(keyBytes, true) &&
        mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, keyBytes, 256) == 0 &&
        mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT, size,
            envelope + 1, 12, aad, sizeof(aad), static_cast<const uint8_t*>(data),
            envelope + 29, 16, envelope + 13) == 0 &&
        mbedtls_base64_encode(reinterpret_cast<uint8_t*>(encoded), sizeof(encoded), &encodedSize,
            envelope, size + 29) == 0;
    if (ok) ok = ConfigService::instance().putSecretString(emailRecordKey(slot).c_str(), encoded);
    mbedtls_gcm_free(&context); mbedtls_platform_zeroize(keyBytes, sizeof(keyBytes));
    mbedtls_platform_zeroize(envelope, sizeof(envelope));
    return ok;
}
bool CredentialStore::readEmailRecord(uint8_t slot, void* data, size_t size) {
    if (!data || !size || size > 512) return false;
    mbedtls_platform_zeroize(data, size);
    if (slot >= 5) return false;
    EmailCredentialGuard lock; if (!lock.locked) return false;
    String encoded = ConfigService::instance().getSecretString(emailRecordKey(slot).c_str());
    if (!encoded.length() || encoded.length() > 724) return false;
    uint8_t envelope[1 + 12 + 16 + 512]{}, keyBytes[32]{}; size_t decoded = 0;
    const uint8_t aad[] = {1, slot};
    mbedtls_gcm_context context; mbedtls_gcm_init(&context);
    const bool ok = mbedtls_base64_decode(envelope, sizeof(envelope), &decoded,
            reinterpret_cast<const uint8_t*>(encoded.c_str()), encoded.length()) == 0 &&
        decoded == size + 29 && envelope[0] == 1 && emailKey(keyBytes, false) &&
        mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, keyBytes, 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&context, size, envelope + 1, 12, aad, sizeof(aad),
            envelope + 13, 16, envelope + 29, static_cast<uint8_t*>(data)) == 0;
    mbedtls_gcm_free(&context); mbedtls_platform_zeroize(keyBytes, sizeof(keyBytes));
    mbedtls_platform_zeroize(envelope, sizeof(envelope));
    if (!ok) mbedtls_platform_zeroize(data, size);
    return ok;
}
bool CredentialStore::removeEmailRecord(uint8_t slot) {
    if (slot >= 5) return false;
    EmailCredentialGuard lock;
    return lock.locked && ConfigService::instance().removeSecret(emailRecordKey(slot).c_str());
}
namespace {
bool oauthId(const char* id){if(!id||!id[0]||strlen(id)>24)return false;
    for(const char* p=id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'))return false;return true;}
}
bool CredentialStore::writeOAuthRecord(const char* id,const void* input,size_t size){
    if(!oauthId(id)||!input||!size||size>12288)return false;
    EmailCredentialGuard lock;if(!lock.locked)return false;
    std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[size+29]);if(!buffer)return false;
    uint8_t keyBytes[32]{};buffer[0]=1;esp_fill_random(buffer.get()+1,12);
    mbedtls_gcm_context context;mbedtls_gcm_init(&context);
    bool ok=emailKey(keyBytes,true)&&mbedtls_gcm_setkey(&context,MBEDTLS_CIPHER_ID_AES,keyBytes,256)==0&&
        mbedtls_gcm_crypt_and_tag(&context,MBEDTLS_GCM_ENCRYPT,size,buffer.get()+1,12,
            reinterpret_cast<const uint8_t*>(id),strlen(id),static_cast<const uint8_t*>(input),buffer.get()+29,16,buffer.get()+13)==0;
    if(ok)ok=StorageService::instance().writeAtomic(StorageArea::Config,String("/credentials/")+id+".bin",buffer.get(),size+29);
    mbedtls_gcm_free(&context);mbedtls_platform_zeroize(keyBytes,sizeof(keyBytes));mbedtls_platform_zeroize(buffer.get(),size+29);return ok;
}
bool CredentialStore::readOAuthRecord(const char* id,void* output,size_t size){
    if(!output||!size||size>12288)return false;mbedtls_platform_zeroize(output,size);
    if(!oauthId(id))return false;EmailCredentialGuard lock;if(!lock.locked)return false;
    std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[size+30]);if(!buffer)return false;
    uint8_t keyBytes[32]{};mbedtls_gcm_context context;mbedtls_gcm_init(&context);
    const bool keyReady=emailKey(keyBytes,false)&&mbedtls_gcm_setkey(&context,MBEDTLS_CIPHER_ID_AES,keyBytes,256)==0;
    auto authenticate=[&](size_t actual){
        return actual==size+29&&buffer[0]==1&&keyReady&&
            mbedtls_gcm_auth_decrypt(&context,size,buffer.get()+1,12,reinterpret_cast<const uint8_t*>(id),strlen(id),
                buffer.get()+13,16,buffer.get()+29,static_cast<uint8_t*>(output))==0;
    };
    auto& storage=StorageService::instance();const String path=String("/credentials/")+id+".bin";
    // Only authenticated complete files can be promoted after power loss.
    // With a missing key leave backups untouched; never generate a replacement.
    bool recovered=false;
    if(keyReady)recovered=storage.recoverAtomicWrite(StorageArea::Config,path,[&](fs::FS& fs,const String& target){
        File file=fs.open(target,FILE_READ);if(!file)return false;
        const size_t length=file.size();const size_t count=length==size+29?file.read(buffer.get(),size+29):0;file.close();
        const bool valid=authenticate(count);mbedtls_platform_zeroize(output,size);return valid;
    });
    size_t actual=0;
    const bool ok=recovered&&storage.read(StorageArea::Config,path,buffer.get(),size+30,&actual)&&authenticate(actual);
    mbedtls_gcm_free(&context);mbedtls_platform_zeroize(keyBytes,sizeof(keyBytes));mbedtls_platform_zeroize(buffer.get(),size+30);
    if(!ok)mbedtls_platform_zeroize(output,size);return ok;
}
bool CredentialStore::removeOAuthRecord(const char* id){
    if(!oauthId(id))return false;EmailCredentialGuard lock;
    if(!lock.locked)return false;auto& storage=StorageService::instance();const String path=String("/credentials/")+id+".bin";
    bool ok=true;
    const String targets[]={path,String(path+".bak"),String(path+".tmp")};
    for(const String& target:targets)
        if(storage.exists(StorageArea::Config,target)&&!storage.remove(StorageArea::Config,target))ok=false;
    return ok;
}
}
