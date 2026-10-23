#pragma once
#include <cstdint>
#include <cstring>
uint32_t millis();
inline size_t strlcpy(char* out,const char* text,size_t capacity){size_t n=strlen(text);if(capacity){size_t copied=n<capacity-1?n:capacity-1;memcpy(out,text,copied);out[copied]=0;}return n;}
