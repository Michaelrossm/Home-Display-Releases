#pragma once
#include <algorithm>
#include <cstdint>
namespace hp {
// Monotonic unsigned subtraction handles the millis() wrap. A synchronous
// transport cannot be interrupted; bound its NEXT call by this remaining time.
class RequestDeadline {
    uint32_t start_,limit_;
public:
    RequestDeadline(uint32_t start,uint32_t limit):start_(start),limit_(limit){}
    uint32_t remaining(uint32_t now)const{
        const uint32_t elapsed=now-start_;return elapsed>=limit_?0:limit_-elapsed;
    }
    uint32_t stepTimeout(uint32_t now,uint32_t configured)const{return std::min(configured,remaining(now));}
};
}
