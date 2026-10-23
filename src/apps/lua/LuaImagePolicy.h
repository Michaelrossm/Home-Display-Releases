#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <cstring>
namespace hp { namespace LuaImagePolicy {
constexpr size_t maxFileBytes=128*1024,maxPixelBytes=256*256*4,maxAssets=16,maxPackageBytes=512*1024;
inline bool validPath(const std::string& path) {
    if(path.compare(0,7,"assets/")||path.size()>63||path.size()<12)return false;
    const auto name=path.substr(7);const auto dot=name.rfind('.');if(name[0]=='.'||name.find("..")!=std::string::npos||dot==name.npos)return false;
    const auto ext=name.substr(dot);if(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg"&&ext!=".bmp"&&ext!=".gif"&&ext!=".webp"&&ext!=".tga")return false;
    for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;
}
inline bool pngSize(const uint8_t* data,size_t size,uint32_t& width,uint32_t& height,uint32_t dimensionLimit=256,size_t fileLimit=maxFileBytes) {
    constexpr uint8_t magic[]={137,80,78,71,13,10,26,10};
    if(!data||size<33||size>fileLimit)return false;
    for(size_t i=0;i<8;++i)if(data[i]!=magic[i])return false;
    if(data[8]||data[9]||data[10]||data[11]!=13||data[12]!='I'||data[13]!='H'||data[14]!='D'||data[15]!='R')return false;
    auto be=[](const uint8_t* p){return(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];};
    width=be(data+16);height=be(data+20);return width&&height&&width<=dimensionLimit&&height<=dimensionLimit;
}
// Strip non-rendering ancillary chunks before libpng sees them. In particular,
// compressed text/ICC metadata must never inflate into an unbounded allocation.
// CRCs of retained chunks are checked by libpng, not silently ignored here.
inline bool stripMetadata(uint8_t* data,size_t& size,uint32_t dimensionLimit=256,size_t fileLimit=maxFileBytes) {
    uint32_t width=0,height=0;if(!pngSize(data,size,width,height,dimensionLimit,fileLimit))return false;
    size_t read=8,write=8;bool idat=false,ended=false;
    while(read<size){if(size-read<12)return false;
        const uint32_t length=(uint32_t(data[read])<<24)|(uint32_t(data[read+1])<<16)|(uint32_t(data[read+2])<<8)|data[read+3];
        if(length>size-read-12)return false;const uint8_t* type=data+read+4;const size_t bytes=size_t(length)+12;
        const bool header=!memcmp(type,"IHDR",4),palette=!memcmp(type,"PLTE",4),pixels=!memcmp(type,"IDAT",4),end=!memcmp(type,"IEND",4);
        const bool alpha=!memcmp(type,"tRNS",4),gamma=!memcmp(type,"gAMA",4),srgb=!memcmp(type,"sRGB",4),chroma=!memcmp(type,"cHRM",4);
        if((header&&read!=8)||(end&&(length||read+bytes!=size)))return false;
        if((gamma&&length!=4)||(srgb&&length!=1)||(chroma&&length!=32)||(alpha&&length>256)||(palette&&length>768))return false;
        const bool keep=header||palette||pixels||end||alpha||gamma||srgb||chroma;
        if(!keep&&!(type[0]&32))return false; // unknown critical chunk
        if(keep){memmove(data+write,data+read,bytes);write+=bytes;}
        idat|=pixels;ended|=end;read+=bytes;
    }
    if(!idat||!ended)return false;size=write;return true;
}
} }
