#pragma once
#include <cstdint>
namespace hp {
// Initial request is not a confirmation. Two later, distinct user actions required.
class SdFormatConfirmation {
    uint8_t stage_=0;uint32_t expires_=0,changed_=0;
public:
    void reset(){stage_=0;expires_=0;}
    void begin(uint32_t now){stage_=1;expires_=now+60000;changed_=now;}
    uint8_t stage(uint32_t now){if(stage_&&static_cast<int32_t>(now-expires_)>=0)reset();return stage_;}
    bool confirm(uint32_t now){const auto s=stage(now);if(static_cast<uint32_t>(now-changed_)<1000)return false;if(s==1){stage_=2;changed_=now;return false;}if(s==2){reset();return true;}return false;}
};
}
