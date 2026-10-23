#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
namespace hp {
// Shared limits for native photo rendering; Lua retains its own smaller limits.
struct ImageRenderPolicy {
    static constexpr uint32_t maxEdge=1920,maxPixels=2000000,previewEdge=480;
    static bool quality(uint32_t edge){return edge==800||edge==1280||edge==1600||edge==1920;}
    struct Size{uint32_t width=0,height=0;};
    static Size fit(uint32_t w,uint32_t h,uint32_t boxW,uint32_t boxH){
        if(!w||!h||!boxW||!boxH)return {};
        uint32_t numerator=1,denominator=1;
        if(w>boxW){numerator=boxW;denominator=w;}
        if(uint64_t(h)*numerator>uint64_t(boxH)*denominator){numerator=boxH;denominator=h;}
        Size out{uint32_t(uint64_t(w)*numerator/denominator),uint32_t(uint64_t(h)*numerator/denominator)};
        if(!out.width)out.width=1;if(!out.height)out.height=1;
        // Integer search keeps aspect ratio and the exact pixel ceiling, including portraits.
        const uint64_t pixelLimit=std::max<uint64_t>(1,std::min<uint64_t>(maxPixels,boxW==boxH&&quality(boxW)?uint64_t(boxW)*boxH*5/8:maxPixels));
        if(uint64_t(out.width)*out.height>pixelLimit){uint32_t low=1,high=out.width;
            while(low<high){const uint32_t mid=(low+high+1)/2;const uint32_t height=uint32_t(uint64_t(out.height)*mid/out.width);
                if(uint64_t(mid)*(height?height:1)<=pixelLimit)low=mid;else high=mid-1;}
            out.height=uint32_t(uint64_t(out.height)*low/out.width);out.width=low;if(!out.height)out.height=1;}
        return out;
    }
    static uint16_t rgb565(uint8_t b,uint8_t g,uint8_t r,uint8_t a=255){
        if(a!=255){b=uint32_t(b)*a/255;g=uint32_t(g)*a/255;r=uint32_t(r)*a/255;}
        return uint16_t((uint16_t(r>>3)<<11)|(uint16_t(g>>2)<<5)|(b>>3));
    }
};
}
