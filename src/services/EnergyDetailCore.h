#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace hp {
// Stable binary v1: IEEE754 floats, little endian; never serialize strings.
struct DetailRecord {
    uint32_t timestamp=0;
    float values[7]={NAN,NAN,NAN,NAN,NAN,NAN,NAN};
    uint16_t intervalSeconds=300,sourceSeconds=0,quality=0,historyFields=0;
};
static_assert(sizeof(DetailRecord)==40,"detail record format changed");
inline uint8_t detailInterval(uint8_t minutes){return minutes>=1&&minutes<=30?minutes:5;}
inline uint32_t detailBucket(uint32_t time,uint32_t step){return step?time-time%step:time;}
struct DetailEstimate {
    uint32_t recordBytes,pointsPerDay;uint64_t bytesPerMonth,bytesPerYear;
};
inline DetailEstimate detailEstimate(uint8_t minutes){const uint32_t n=1440/detailInterval(minutes);return {40,n,static_cast<uint64_t>(n)*40*31,static_cast<uint64_t>(n)*40*366};}
// Caller places this bounded buffer in PSRAM. No allocation or I/O in sampling.
class DetailBuffer {
public:
    static constexpr size_t Capacity=96;
    struct Slot {
        DetailRecord record;
        double sums[4]{};uint32_t weights[4]{};
        uint32_t latest[7]{},lastHistory=0;
    };
    std::array<Slot,Capacity> slots{};size_t count=0;uint32_t dropped=0;
    void add(uint32_t timestamp,const float* values,uint16_t quality,uint16_t sourceSeconds,uint8_t minutes,bool history){
        if(!timestamp||!values||!sourceSeconds)return;
        bool available=false;for(unsigned i=0;i<7;++i)if(std::isfinite(values[i])&&((quality>>(i*2))&3))available=true;if(!available)return;
        const uint32_t step=std::max<uint32_t>(detailInterval(minutes)*60,sourceSeconds);
        const uint32_t bucket=detailBucket(timestamp,step);
        Slot* slot=nullptr;
        for(size_t i=0;i<count;++i)if(slots[i].record.timestamp==bucket){slot=&slots[i];break;}
        if(!slot){if(count==Capacity){++dropped;if(bucket<slots[0].record.timestamp)return;erase(0);}size_t pos=count;while(pos&&slots[pos-1].record.timestamp>bucket){slots[pos]=slots[pos-1];--pos;}
            slot=&slots[pos];*slot=Slot{};slot->record.timestamp=bucket;slot->record.intervalSeconds=step;++count;}
        auto& r=slot->record;
        // Imported hourly averages must not replace locally sampled minutes,
        // even when their quality flag is higher. Resolution wins first.
        if(history&&r.quality&&step>r.intervalSeconds)return;
        if(history&&r.historyFields&&step>=r.intervalSeconds&&sourceSeconds>r.sourceSeconds)return;
        const bool refinement=history&&r.historyFields&&(step<r.intervalSeconds||sourceSeconds<r.sourceSeconds);
        const bool repeated=history&&!refinement&&timestamp<=slot->lastHistory;
        for(unsigned i=0;i<7;++i){if(!std::isfinite(values[i]))continue;
            const unsigned old=(r.quality>>(i*2))&3,next=(quality>>(i*2))&3;
            if(!next||next<old||(!history&&(r.historyFields&(1u<<i))))continue;
            if(repeated&&(r.historyFields&(1u<<i))&&next<=old)continue;
            const bool replace=history&&(refinement||!(r.historyFields&(1u<<i)));
            if(i<4){if(replace||next>old){slot->sums[i]=0;slot->weights[i]=0;}
                slot->sums[i]+=static_cast<double>(values[i])*sourceSeconds;slot->weights[i]+=sourceSeconds;
                r.values[i]=static_cast<float>(slot->sums[i]/slot->weights[i]);
            }else if(replace||timestamp>=slot->latest[i]){r.values[i]=values[i];slot->latest[i]=timestamp;}
            r.quality=(r.quality&~(3u<<(i*2)))|(next<<(i*2));
            if(history)r.historyFields|=1u<<i;
        }
        if(history){slot->lastHistory=refinement?timestamp:std::max(slot->lastHistory,timestamp);r.sourceSeconds=sourceSeconds;r.intervalSeconds=step;}
        else if(!r.sourceSeconds)r.sourceSeconds=sourceSeconds;
    }
    void erase(size_t index){if(index>=count)return;for(size_t i=index+1;i<count;++i)slots[i-1]=slots[i];--count;}
};
inline uint32_t detailCrc(const void* input,size_t size){uint32_t crc=0xffffffff;auto* bytes=static_cast<const uint8_t*>(input);while(size--){crc^=*bytes++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}return ~crc;}
inline bool detailMerge(DetailRecord& old,const DetailRecord& input){bool changed=false;
    const auto oldPeriod=std::max(old.sourceSeconds,old.intervalSeconds),newPeriod=std::max(input.sourceSeconds,input.intervalSeconds);
    if(old.quality&&newPeriod>oldPeriod)return false;
    for(unsigned i=0;i<7;++i){if(!std::isfinite(input.values[i]))continue;const unsigned a=(old.quality>>(i*2))&3,b=(input.quality>>(i*2))&3;
        const bool betterHistory=(input.historyFields&(1u<<i))&&(!(old.historyFields&(1u<<i))||input.sourceSeconds<old.sourceSeconds);
        if(!b||b<a||(b==a&&!betterHistory&&newPeriod>=oldPeriod))continue;
        old.values[i]=input.values[i];old.quality=(old.quality&~(3u<<(i*2)))|(b<<(i*2));
        old.historyFields=(old.historyFields&~(1u<<i))|(input.historyFields&(1u<<i));changed=true;
    }
    if(changed){old.sourceSeconds=input.sourceSeconds;old.intervalSeconds=std::min(old.intervalSeconds,input.intervalSeconds);}return changed;
}
struct DetailFileHeader {uint32_t magic=0x31444845;uint16_t version=1,recordBytes=sizeof(DetailRecord);};
struct DetailBlockHeader {uint32_t magic=0x31424c45;uint16_t count=0,reserved=0;uint32_t payloadCrc=0,headerCrc=0;};
inline bool detailHeaderValid(const DetailBlockHeader& header){auto copy=header;copy.headerCrc=0;return header.magic==0x31424c45&&header.count>0&&header.count<=96&&header.headerCrc==detailCrc(&copy,sizeof(copy));}
inline void detailSeal(DetailBlockHeader& header,const DetailRecord* records,size_t count){header=DetailBlockHeader{};header.count=count;header.payloadCrc=detailCrc(records,count*sizeof(*records));header.headerCrc=detailCrc(&header,sizeof(header));}
}
