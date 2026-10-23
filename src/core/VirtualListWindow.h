#pragma once
#include <algorithm>
#include <cstdint>
namespace hp {
struct VirtualListWindow {
    static constexpr unsigned kRows=24,kPage=96,kRowHeight=64;
    uint32_t first=0,count=0;
    static VirtualListWindow at(uint32_t scroll,uint32_t height,uint32_t total){
        const uint32_t visible=(std::min<uint32_t>(height,800)+kRowHeight-1)/kRowHeight;
        const uint32_t top=scroll/kRowHeight;
        const uint32_t first=top>2?top-2:0;
        return {std::min(first,total),std::min<uint32_t>(std::min<uint32_t>(visible+4,kRows),total>first?total-first:0)};
    }
    bool cached(uint32_t offset,uint32_t size)const{return !count||(first>=offset&&uint64_t(first)+count<=uint64_t(offset)+size);}
};
}
