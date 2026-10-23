#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
namespace hp {
inline bool photoWindow(bool clockValid,unsigned weekday,unsigned minute,uint8_t days,unsigned start,unsigned end){
    if(!clockValid||weekday>6||minute>=1440||start>=1440||end>=1440)return false;
    if(start==end)return (days&(1u<<weekday))!=0;
    if(start<end)return (days&(1u<<weekday))&&minute>=start&&minute<end;
    return minute>=start?(days&(1u<<weekday)):(minute<end&&(days&(1u<<((weekday+6)%7))));
}
class PhotoShuffleBag {
    std::vector<uint16_t> bag_;size_t cursor_=0;uint16_t last_=UINT16_MAX;
    template<class Random> static uint32_t bounded(Random& random,uint32_t n){const uint32_t threshold=uint32_t(-n)%n;uint32_t v=0;for(unsigned retry=0;retry<32;++retry){v=random();if(v>=threshold)return v%n;}return v%n;}
public:
    void reset(){bag_.clear();cursor_=0;last_=UINT16_MAX;}
    template<class Random> uint16_t next(size_t count,Random random){
        if(!count||count>1024)return UINT16_MAX;
        if(bag_.size()!=count){bag_.resize(count);cursor_=count;last_=UINT16_MAX;}
        if(cursor_>=count){for(size_t i=0;i<count;++i)bag_[i]=i;
            for(size_t i=count;i>1;--i)std::swap(bag_[i-1],bag_[bounded(random,i)]);
            if(count>1&&bag_[0]==last_)std::swap(bag_[0],bag_[1+bounded(random,count-1)]);cursor_=0;}
        last_=bag_[cursor_++];return last_;
    }
};
}
