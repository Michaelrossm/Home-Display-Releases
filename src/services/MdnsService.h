#pragma once
#include <Arduino.h>
namespace hp { class MdnsService{public:static MdnsService&instance();bool start(const char*host);void stop();void loop();bool active()const{return on_;}private:bool on_=false;String hostname_,address_;uint32_t checkedAt_=0;}; }
