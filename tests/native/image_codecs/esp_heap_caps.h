#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline void* heap_caps_malloc(size_t n,int){return std::malloc(n);}
inline void* heap_caps_calloc(size_t n,size_t size,int){return std::calloc(n,size);}
inline void* heap_caps_realloc(void* p,size_t n,int){return std::realloc(p,n);}
inline void heap_caps_free(void* p){std::free(p);}
inline size_t heap_caps_get_free_size(int){return 32*1024*1024;}
inline size_t heap_caps_get_largest_free_block(int){return 32*1024*1024;}
