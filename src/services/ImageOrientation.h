#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace hp {
inline void imageSourcePoint(uint32_t w,uint32_t h,unsigned orientation,uint32_t x,uint32_t y,uint32_t& px,uint32_t& py){
    px=x;py=y;switch(orientation){case 2:px=w-1-x;break;case 3:px=w-1-x;py=h-1-y;break;case 4:py=h-1-y;break;
        case 5:px=y;py=x;break;case 6:px=y;py=h-1-x;break;case 7:px=w-1-y;py=h-1-x;break;case 8:px=w-1-y;py=x;break;default:break;}
}
// Bounded EXIF reader: no TIFF image decoder, external data or offset guessing.
inline unsigned jpegOrientation(const uint8_t* bytes,size_t size){
    if(!bytes||size<4||bytes[0]!=255||bytes[1]!=216)return 1;size_t pos=2;
    while(pos+4<=size){if(bytes[pos++]!=255)return 1;while(pos<size&&bytes[pos]==255)++pos;if(pos>=size)return 1;const uint8_t marker=bytes[pos++];
        if(marker==218||marker==217)return 1;if(marker==1||(marker>=208&&marker<=215))continue;
        if(size-pos<2)return 1;const size_t length=(size_t(bytes[pos])<<8)|bytes[pos+1];if(length<2||length>size-pos)return 1;
        if(marker==225&&length>=16&&!memcmp(bytes+pos+2,"Exif\0\0",6)){
            const uint8_t* tiff=bytes+pos+8;const size_t available=length-8;const bool little=tiff[0]=='I'&&tiff[1]=='I';
            if(!little&&!(tiff[0]=='M'&&tiff[1]=='M'))return 1;
            auto u16=[&](const uint8_t* p){return little?uint32_t(p[0])|(uint32_t(p[1])<<8):(uint32_t(p[0])<<8)|p[1];};
            auto u32=[&](const uint8_t* p){return little?u16(p)|(u16(p+2)<<16):(u16(p)<<16)|u16(p+2);};
            if(u16(tiff+2)!=42)return 1;const size_t offset=u32(tiff+4);if(offset>available||available-offset<2)return 1;
            const size_t count=u16(tiff+offset);if(count>512||count>(available-offset-2)/12)return 1;
            for(size_t i=0;i<count;++i){const auto* entry=tiff+offset+2+i*12;if(u16(entry)==274&&u16(entry+2)==3&&u32(entry+4)==1){const unsigned value=u16(entry+8);return value>=1&&value<=8?value:1;}}
        }pos+=length;
    }return 1;
}
}
