#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace hp::webLogin {
constexpr uint32_t rememberSeconds=30u*24*60*60,sessionSeconds=12u*60*60;
struct Session {uint8_t digest[32]{};uint64_t issued=0,expires=0,started=0;uint8_t remembered=0;uint8_t reserved[7]{};};
inline bool equal(const uint8_t* a,const uint8_t* b,size_t size){unsigned diff=0;for(size_t i=0;i<size;++i)diff|=a[i]^b[i];return diff==0;}
inline bool active(const Session& s,uint64_t now,uint64_t tick){if(!s.issued)return false;if(s.remembered)return s.remembered==1&&s.issued>=1700000000&&s.expires==s.issued+rememberSeconds&&now>=s.issued&&now<s.expires;return tick>=s.started&&tick-s.started<uint64_t(sessionSeconds)*1000;}
inline uint32_t lockSeconds(unsigned failures){return failures>=20?1800:failures>=15?600:failures>=10?300:failures>=5?120:0;}
inline bool token(const char* value){if(!value||strlen(value)!=64)return false;for(const char* p=value;*p;++p)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return false;return true;}
inline bool next(const char* value){if(!value||*value!='/'||value[1]=='/'||strlen(value)>1024)return false;for(const char* p=value;*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127||*p=='\\')return false;return true;}
}
