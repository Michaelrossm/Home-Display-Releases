#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace hp::intercom {
inline uint32_t videoRetryDelay(const char* code,unsigned attempts){
    if(!code||attempts>=3)return 0;
    for(const char* transient:{"video_connect","video_disconnected","video_timeout","video_offline","rtsp_keepalive_timeout"})
        if(!std::strcmp(code,transient))return 1000u<<attempts;
    return 0; // Authentication, codec, protocol, memory and commands are never retried.
}
}
