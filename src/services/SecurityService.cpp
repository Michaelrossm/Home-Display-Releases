#include "SecurityService.h"
#include "ConfigService.h"
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <algorithm>
namespace hp {
namespace {
const char* secretKeys[]={"sec_set_pin","sec_tab_pin"};
const char* failKeys[]={"sec_set_fail","sec_tab_fail"};
unsigned index(PinKind kind){return kind==PinKind::Tablet?1:0;}
uint32_t deadline(uint32_t delay){const uint32_t until=millis()+delay;return until?until:1;}
String digest(const String& salt,const String& pin){
    uint8_t hash[32];String input=salt+":"+pin;
    if(mbedtls_sha256(reinterpret_cast<const uint8_t*>(input.c_str()),input.length(),hash,0)!=0)return "";
    for(unsigned i=0;i<2048;++i)if(mbedtls_sha256(hash,sizeof(hash),hash,0)!=0)return "";
    char out[65];for(unsigned i=0;i<32;++i)snprintf(out+i*2,3,"%02x",hash[i]);
    memset(hash,0,sizeof(hash));return out;
}
bool equal(const String& a,const String& b){if(a.length()!=b.length())return false;uint8_t diff=0;for(size_t i=0;i<a.length();++i)diff|=a[i]^b[i];return diff==0;}
bool verifierValid(const String& text){if(text.length()!=97||text[32]!=':')return false;for(size_t i=0;i<97;++i)if(i!=32&&!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return false;return true;}
}
SecurityService& SecurityService::instance(){static SecurityService service;return service;}
bool SecurityService::begin(){
    if(ready_)return true;
    if(!mutex_)mutex_=xSemaphoreCreateRecursiveMutex();if(!mutex_)return false;
    SecurityWriteGuard guard;auto& config=ConfigService::instance();if(!config.writable()||!config.credentialsReady())return false;
    for(unsigned i=0;i<2;++i){states_[i].verifier=config.getSecretString(secretKeys[i]);
        if(config.hasSecret(secretKeys[i])&&!verifierValid(states_[i].verifier))return false;
        const uint32_t attempts=config.getU32(failKeys[i]);states_[i].failures=std::min<uint32_t>(20,attempts>>16);
        const uint32_t wait=std::min<uint32_t>(1800,attempts&0xffff);states_[i].until=wait?deadline(wait*1000):0;states_[i].unlocked=false;}
    offLock_=config.getBool("sec_off_lock",true);ready_=true;return true;
}
bool SecurityService::validPin(const String& pin){if(pin.length()<4||pin.length()>12)return false;for(size_t i=0;i<pin.length();++i)if(pin[i]<'0'||pin[i]>'9')return false;return true;}
bool SecurityService::enabled(PinKind kind){SecurityWriteGuard guard;return !ready_||states_[index(kind)].verifier.length()>0;}
uint32_t SecurityService::blockedSeconds(PinKind kind){SecurityWriteGuard guard;auto& s=states_[index(kind)];if(!s.until)return 0;const int32_t left=static_cast<int32_t>(s.until-millis());if(left<=0){s.until=0;return 0;}return (static_cast<uint32_t>(left)+999)/1000;}
bool SecurityService::persistAttempts(PinKind kind,uint32_t seconds){auto& c=ConfigService::instance();unsigned i=index(kind);return c.putU32(failKeys[i],(states_[i].failures<<16)|seconds);}
bool SecurityService::verify(PinKind kind,const String& pin){
    if(!ready_||blockedSeconds(kind))return false;auto& s=states_[index(kind)];
    if(!s.verifier.length())return true;
    const bool ok=validPin(pin)&&s.verifier.length()==97&&equal(digest(s.verifier.substring(0,32),pin),s.verifier.substring(33));
    if(ok){const uint32_t previous=s.failures;s.failures=0;if(!persistAttempts(kind,0)){s.failures=previous;s.until=deadline(1800000);s.unlocked=false;return false;}s.until=0;return true;}
    if(s.failures<20)++s.failures;
    uint32_t wait=0;
    if(s.failures>=20)wait=1800;else if(s.failures==15)wait=600;else if(s.failures==10)wait=300;else if(s.failures==5)wait=120;
    s.until=wait?deadline(wait*1000):0;
    if(!persistAttempts(kind,wait)){s.until=deadline(1800000);}
    return false;
}
bool SecurityService::unlock(PinKind kind,const String& pin){SecurityWriteGuard guard;if(!verify(kind,pin))return false;states_[index(kind)].unlocked=true;return true;}
bool SecurityService::setPin(PinKind kind,const String& oldPin,const String& pin,const String& repeat){
    SecurityWriteGuard guard;if(!ready_||tabletLocked()||!validPin(pin)||pin!=repeat||!verify(kind,oldPin))return false;
    uint8_t salt[16];esp_fill_random(salt,sizeof(salt));char text[33];for(unsigned i=0;i<16;++i)snprintf(text+i*2,3,"%02x",salt[i]);
    String hash=digest(text,pin);if(hash.length()!=64)return false;String verifier=String(text)+":"+hash;
    if(!ConfigService::instance().putSecretString(secretKeys[index(kind)],verifier))return false;
    auto& s=states_[index(kind)];s.verifier=verifier;s.failures=0;s.until=0;s.unlocked=kind==PinKind::Tablet;return persistAttempts(kind,0);
}
bool SecurityService::removePin(PinKind kind,const String& pin){SecurityWriteGuard guard;if(!ready_||tabletLocked()||!verify(kind,pin))return false;if(!ConfigService::instance().removeSecret(secretKeys[index(kind)]))return false;auto& s=states_[index(kind)];s.verifier="";s.unlocked=false;s.failures=0;s.until=0;return persistAttempts(kind,0);}
void SecurityService::lockSettings(){SecurityWriteGuard guard;states_[0].unlocked=false;}
void SecurityService::lockTablet(){SecurityWriteGuard guard;states_[1].unlocked=false;states_[0].unlocked=false;}
bool SecurityService::tabletLocked(){SecurityWriteGuard guard;return !ready_||(states_[1].verifier.length()&&!states_[1].unlocked);}
bool SecurityService::settingsAllowed(){SecurityWriteGuard guard;return ready_&&!tabletLocked()&&(!states_[0].verifier.length()||states_[0].unlocked);}
bool SecurityService::lockAfterDisplayOff(){SecurityWriteGuard guard;return offLock_;}
bool SecurityService::setLockAfterDisplayOff(bool value){SecurityWriteGuard guard;if(!settingsAllowed()||!ConfigService::instance().putBool("sec_off_lock",value))return false;offLock_=value;return true;}
}
