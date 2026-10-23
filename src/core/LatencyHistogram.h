#pragma once
#include <atomic>
#include <cstdint>

namespace hp {
// Fixed RAM, concurrent writers; logarithmic millisecond buckets. Quantiles
// are bucket upper bounds, not exact measurements. No allocation or logging.
class LatencyHistogram {
public:
    struct Snapshot { uint32_t samples=0,p50Ms=0,p95Ms=0,p99Ms=0,maxMs=0; };
    void add(uint32_t ms) {
        unsigned bin=0;
        for(uint32_t value=ms;value;value>>=1)++bin;
        auto old=max_.load(std::memory_order_relaxed);
        while(ms>old&&!max_.compare_exchange_weak(old,ms,std::memory_order_relaxed)){}
        bins_[bin].fetch_add(1,std::memory_order_relaxed);
    }
    Snapshot snapshot() const {
        Snapshot out;uint32_t values[33];uint64_t total=0;
        for(unsigned i=0;i<33;++i){values[i]=bins_[i].load(std::memory_order_relaxed);total+=values[i];}
        out.samples=total>UINT32_MAX?UINT32_MAX:static_cast<uint32_t>(total);
        out.maxMs=max_.load(std::memory_order_relaxed);
        if(!total)return out;
        uint64_t seen=0;bool p50=false,p95=false,p99=false;
        for(unsigned i=0;i<33;++i){
            seen+=values[i];const uint32_t upper=i==32?UINT32_MAX:(uint32_t(1)<<i)-1;
            const uint32_t bounded=upper<out.maxMs?upper:out.maxMs;
            if(!p50&&seen*100>=total*50){out.p50Ms=bounded;p50=true;}
            if(!p95&&seen*100>=total*95){out.p95Ms=bounded;p95=true;}
            if(!p99&&seen*100>=total*99){out.p99Ms=bounded;p99=true;}
        }
        return out;
    }
private:
    std::atomic<uint32_t> bins_[33]{};
    std::atomic<uint32_t> max_{0};
};
}
