#pragma once
#include <cstdint>
#include <Arduino.h>
namespace hp {
inline bool printerThumbnailBase64(const String& text,String& bytes){
    bytes="";if(!text.length()||text.length()>90000||text.length()%4)return false;
    bytes.reserve(text.length()/4*3);
    for(size_t p=0;p<text.length();p+=4){
        uint32_t value=0;unsigned padding=0;
        for(unsigned j=0;j<4;++j){const char c=text[p+j];unsigned n=0;
            if(c>='A'&&c<='Z')n=c-'A';else if(c>='a'&&c<='z')n=c-'a'+26;else if(c>='0'&&c<='9')n=c-'0'+52;
            else if(c=='+')n=62;else if(c=='/')n=63;
            else if(c=='='&&j>=2&&p+4==text.length())++padding;else return false;
            if(padding&&c!='=')return false;value=value<<6|n;
        }
        if(padding==2&&(value&65535)||padding==1&&(value&255))return false;
        bytes+=char(value>>16);if(padding<2)bytes+=char(value>>8);if(!padding)bytes+=char(value);
    }
    return bytes.length()&&bytes.length()<=65536;
}
// Prefer the smallest sufficient thumbnail, otherwise the largest available.
// Shared by adapters and local slicer-comment extraction; no G-code download.
struct PrinterThumbnailChoice {
    uint32_t width=320,height=220,bestWidth=0,bestHeight=0;
    bool consider(uint32_t w,uint32_t h){
        if(!w||!h||w>512||h>512)return false;
        const bool enough=w>=width&&h>=height,previous=bestWidth>=width&&bestHeight>=height;
        if(bestWidth&&((previous&&!enough)||(previous==enough&&(enough?uint64_t(w)*h>=uint64_t(bestWidth)*bestHeight:uint64_t(w)*h<=uint64_t(bestWidth)*bestHeight))))return false;
        bestWidth=w;bestHeight=h;return true;
    }
};
}
