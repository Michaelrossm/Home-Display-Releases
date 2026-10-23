#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
namespace hp {
// Native, monotonic bounded history. Downsampling retains both extrema in
// chronological order; no Lua history table and no unbounded sample growth.
struct ChartHistoryWindow {
    static constexpr unsigned capacity=1024;
    struct Sample{int64_t time=0;int32_t value=0;};
    std::array<Sample,capacity> samples{};unsigned head=0,count=0;
    bool append(int64_t time,int32_t value){
        if(count){auto& last=samples[(head+capacity-1)%capacity];if(time<last.time)return false;if(time==last.time){last.value=value;return true;}}
        samples[head]={time,value};head=(head+1)%capacity;count=std::min(capacity,count+1);return true;
    }
    unsigned range(int64_t from,int64_t to,Sample* output,unsigned limit)const{
        if(!output||limit<2||limit>256||to<from)return 0;
        unsigned first=0,last=count;auto at=[&](unsigned i)->const Sample&{return samples[(head+capacity-count+i)%capacity];};
        while(first<last&&at(first).time<from)++first;while(last>first&&at(last-1).time>to)--last;
        const unsigned size=last-first;if(size<=limit){for(unsigned i=0;i<size;++i)output[i]=at(first+i);return size;}
        unsigned written=0;const unsigned buckets=limit/2;
        for(unsigned b=0;b<buckets;++b){const unsigned begin=first+uint64_t(b)*size/buckets,end=first+uint64_t(b+1)*size/buckets;
            unsigned low=begin,high=begin;for(unsigned i=begin+1;i<end;++i){if(at(i).value<at(low).value)low=i;if(at(i).value>at(high).value)high=i;}
            if(low>high)std::swap(low,high);output[written++]=at(low);if(high!=low)output[written++]=at(high);
        }return written;
    }
};
}
