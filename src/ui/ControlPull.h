#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace hp {

// Pure gesture policy; pixels in logical display coordinates, time in ms.
struct ControlPull {
    int height=1,startAmount=0,amount=0,startX=0,startY=0,lastY=0;
    uint32_t lastTime=0;
    float velocity=0;
    bool active=false,claimed=false,rejected=false;

    void begin(int x,int y,int visible,int h,uint32_t now){
        height=std::max(1,h);
        startAmount=amount=std::max(0,std::min(height,visible));
        startX=x;
        startY=lastY=y;
        lastTime=now;
        velocity=0;
        active=true;
        claimed=false;
        rejected=false;
    }

    bool move(int x,int y,uint32_t now){
        if(!active||rejected)return false;

        const int dx=x-startX;
        const int dy=y-startY;

        if(!claimed){
            if(std::abs(dx)>12&&std::abs(dx)>std::abs(dy)){
                rejected=true;
                return false;
            }
            if(std::abs(dy)<12)return false;

            if((startAmount==0&&dy<0)||(startAmount==height&&dy>0)){
                rejected=true;
                return false;
            }
            claimed=true;
        }

        const uint32_t elapsed=now-lastTime;
        if(y!=lastY){
            if(elapsed) velocity=float(y-lastY)/elapsed;
            lastY=y;
            lastTime=now;
        }

        amount=std::max(0,std::min(height,startAmount+dy));
        return true;
    }

    bool finish(uint32_t now){
        active=false;

        if(!claimed)
            return startAmount>=height/2;

        // A clear halfway crossing always wins, even if the last few pixels
        // moved in the opposite direction before the finger was lifted.
        if(startAmount<height/2 && amount>=height/2)return true;
        if(startAmount>=height/2 && amount<=height/2)return false;

        const int travelled=std::abs(amount-startAmount);
        const bool freshVelocity=static_cast<uint32_t>(now-lastTime)<180UL;

        // A short but intentional pull is enough to latch a full-screen sheet.
        // Do not require a 400px drag on the 800px display. Stale velocities
        // (the finger paused for >=180ms before release) must not force a snap.
        const int deliberate=std::max(36,std::min(80,height/10));
        if(freshVelocity && travelled>=deliberate && std::abs(velocity)>=0.12f)
            return velocity>0;

        return amount>=height/2;
    }
};

}
