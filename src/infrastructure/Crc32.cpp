#include "Crc32.h"
namespace hp { uint32_t crc32(const void*d,size_t n,uint32_t seed){uint32_t c=seed;auto*p=(const uint8_t*)d;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return c^0xffffffffu;} }
