#pragma once
#include <cstdint>
#include <algorithm>
namespace hp {
// LVGL scale: 256 = fitted image. Zoom only; never upscale decode allocations.
struct PhotoZoom {
    uint16_t scale=256,base=256,span=0;
    int32_t panX=0,panY=0,lastX=0,lastY=0;uint8_t fingers=0;
    void reset(){scale=base=256;span=0;panX=panY=lastX=lastY=0;fingers=0;}
    uint16_t update(uint16_t distance){
        if(distance<20){span=0;return scale;}
        if(!span){span=distance;base=scale;return scale;}
        const uint32_t next=uint32_t(base)*distance/span;
        scale=next<256?256:next>1024?1024:uint16_t(next);return scale;
    }
    bool transform(uint8_t count,uint16_t distance,int32_t x,int32_t y,int32_t cx,int32_t cy,int32_t viewportW,int32_t viewportH,int32_t fittedW,int32_t fittedH){
        const auto oldScale=scale;const auto oldX=panX,oldY=panY;
        if(count>=2){const int32_t focusX=cx-viewportW/2,focusY=cy-viewportH/2;update(distance);
            if(fingers>=2){panX=focusX-int64_t(lastX-panX)*scale/oldScale;panY=focusY-int64_t(lastY-panY)*scale/oldScale;}
            lastX=focusX;lastY=focusY;
        }else {update(0);if(count==1){if(fingers==1&&scale>256){panX+=x-lastX;panY+=y-lastY;}lastX=x;lastY=y;}}
        fingers=count;const int32_t limitX=std::max<int32_t>(0,(int64_t(fittedW)*scale/256-viewportW)/2),limitY=std::max<int32_t>(0,(int64_t(fittedH)*scale/256-viewportH)/2);
        panX=std::max(-limitX,std::min(limitX,panX));panY=std::max(-limitY,std::min(limitY,panY));
        return oldScale!=scale||oldX!=panX||oldY!=panY;
    }
};
}
