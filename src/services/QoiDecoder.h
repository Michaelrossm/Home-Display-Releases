#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
namespace hp {
// QOI 1.0, bounded BGRA decoder shared by image resources and printer previews.
inline bool qoiDimensions(const uint8_t* data,size_t bytes,uint32_t& w,uint32_t& h){
    if(!data||bytes<23||memcmp(data,"qoif",4)||(data[12]!=3&&data[12]!=4)||data[13]>1)return false;
    auto be=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    w=be(data+4);h=be(data+8);
    static constexpr uint8_t end[8]={0,0,0,0,0,0,0,1};
    return w&&h&&w<=8192&&h<=8192&&uint64_t(w)*h<=4*1024*1024&&memcmp(data+bytes-8,end,8)==0;
}
inline bool qoiDecodeBgra(const uint8_t* data,size_t bytes,uint8_t* out,size_t capacity,const std::atomic<bool>* cancelled=nullptr){
    uint32_t w,h;if(!out||!qoiDimensions(data,bytes,w,h)||uint64_t(w)*h*4>capacity)return false;
    struct Pixel{uint8_t r=0,g=0,b=0,a=0;};Pixel index[64]{},pixel{0,0,0,255};
    size_t pos=14;unsigned run=0;const size_t end=bytes-8,count=size_t(w)*h;
    for(size_t i=0;i<count;++i){
        if(!(i%4096)&&cancelled&&cancelled->load())return false;
        if(run)--run;
        else{
            if(pos>=end)return false;const uint8_t tag=data[pos++];
            if(tag==254){if(end-pos<3)return false;pixel.r=data[pos++];pixel.g=data[pos++];pixel.b=data[pos++];}
            else if(tag==255){if(end-pos<4)return false;pixel.r=data[pos++];pixel.g=data[pos++];pixel.b=data[pos++];pixel.a=data[pos++];}
            else switch(tag>>6){
                case 0:pixel=index[tag&63];break;
                case 1:pixel.r+=((tag>>4)&3)-2;pixel.g+=((tag>>2)&3)-2;pixel.b+=(tag&3)-2;break;
                case 2:{if(pos>=end)return false;const uint8_t next=data[pos++];const int dg=(tag&63)-32;pixel.r+=dg+(next>>4)-8;pixel.g+=dg;pixel.b+=dg+(next&15)-8;break;}
                case 3:run=tag&63;if(run>count-i-1)return false;break;
            }
        }
        index[(pixel.r*3+pixel.g*5+pixel.b*7+pixel.a*11)%64]=pixel;
        out[i*4]=pixel.b;out[i*4+1]=pixel.g;out[i*4+2]=pixel.r;out[i*4+3]=pixel.a;
    }
    return pos==end&&!run;
}
}
