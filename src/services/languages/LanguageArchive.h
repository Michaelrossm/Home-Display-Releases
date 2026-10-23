#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace hp {
// One deflated locale.json only. Never extracts files or follows archive paths.
struct LanguageArchive {
    const uint8_t* compressed=nullptr;size_t compressedSize=0,size=0;uint32_t crc=0;
    static constexpr size_t maxZip=256*1024,maxRaw=512*1024;
    static uint16_t u16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
    static uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
    bool parse(const uint8_t* zip,size_t length){*this={};
        if(!zip||length<22+30+46+22||length>maxZip)return false;
        const auto* end=zip+length-22;
        if(u32(end)!=0x06054b50||u16(end+4)||u16(end+6)||u16(end+8)!=1||u16(end+10)!=1||u16(end+20))return false;
        const size_t offset=u32(end+16),centralSize=u32(end+12);
        if(offset>length-22||centralSize!=57||offset+centralSize!=length-22)return false;
        const auto* central=zip+offset;
        if(u32(central)!=0x02014b50||(u16(central+8)&~0x800u)||u16(central+10)!=8||u16(central+28)!=11||u16(central+30)||u16(central+32)||u16(central+34)||u32(central+42))return false;
        if(memcmp(central+46,"locale.json",11)||u32(zip)!=0x04034b50||u16(zip+6)!=u16(central+8)||u16(zip+8)!=8||u16(zip+26)!=11||u16(zip+28)||memcmp(zip+30,"locale.json",11))return false;
        const size_t packed=u32(central+20),raw=u32(central+24);
        if(!packed||!raw||raw>maxRaw||packed>offset||41+packed!=offset||u32(zip+18)!=packed||u32(zip+22)!=raw||u32(zip+14)!=u32(central+16))return false;
        compressed=zip+41;compressedSize=packed;size=raw;crc=u32(central+16);return true;
    }
};
}
