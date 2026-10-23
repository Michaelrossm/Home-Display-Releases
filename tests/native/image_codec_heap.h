#pragma once
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
namespace codecHeap {
struct alignas(16) Header{size_t size;};
inline size_t live=0,peak=0,calls=0,failAt=0,stopAt=0;
inline std::atomic<bool>* stop=nullptr;
inline void reset(){peak=live;calls=failAt=stopAt=0;stop=nullptr;}
}
inline void* heap_caps_malloc(size_t bytes,int){using namespace codecHeap;if(++calls==failAt)return nullptr;if(bytes>32*1024*1024-live)return nullptr;auto* h=(Header*)malloc(sizeof(Header)+bytes);if(!h)return nullptr;h->size=bytes;live+=bytes;peak=std::max(peak,live);if(stop&&calls==stopAt)stop->store(true);return h+1;}
inline void heap_caps_free(void* p){if(!p)return;auto* h=(codecHeap::Header*)p-1;codecHeap::live-=h->size;free(h);}
inline void* heap_caps_calloc(size_t n,size_t size,int caps){if(size&&n>SIZE_MAX/size)return nullptr;void* p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
inline void* heap_caps_realloc(void* old,size_t size,int caps){if(!old)return heap_caps_malloc(size,caps);auto* h=(codecHeap::Header*)old-1;void* p=heap_caps_malloc(size,caps);if(p){memcpy(p,old,std::min(size,h->size));heap_caps_free(old);}return p;}
inline size_t heap_caps_get_free_size(int){return 32*1024*1024-codecHeap::live;}
inline size_t heap_caps_get_largest_free_block(int c){return heap_caps_get_free_size(c);}
