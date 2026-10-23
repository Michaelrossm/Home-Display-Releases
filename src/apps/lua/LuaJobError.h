#pragma once
#include <cstring>
namespace hp {
inline const char* luaJobErrorCode(const char* error){
    if(!error||!error[0])return "none";
    if(std::strstr(error,"cancel"))return "cancelled";
    if(std::strstr(error,"timeout"))return "timeout";
    if(std::strstr(error,"locked"))return "locked";
    if(std::strstr(error,"permission"))return "permission_denied";
    if(std::strstr(error,"quota")||std::strstr(error,"limit")||std::strstr(error,"budget"))return "limit_exceeded";
    if(std::strstr(error,"memory"))return "out_of_memory";
    if(std::strstr(error,"secret")||std::strstr(error,"auth"))return "authentication";
    if(std::strstr(error,"busy")||std::strstr(error,"queue"))return "busy";
    if(std::strstr(error,"http_status"))return "http_error";
    if(std::strstr(error,"url")||std::strstr(error,"path")||std::strstr(error,"option")||std::strstr(error,"argument")||std::strstr(error,"range"))return "invalid_argument";
    if(std::strstr(error,"connect")||std::strstr(error,"provider")||std::strstr(error,"unavailable"))return "unavailable";
    if(!std::strncmp(error,"printer_",8))return error;
    return "io_error";
}
}
