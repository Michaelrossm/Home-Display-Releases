#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <esp_heap_caps.h>
namespace hp {
inline bool printerDecodeModel(const String& text,JsonDocument& doc,String& error,unsigned depth=16){
    if(text.length()>65536||deserializeJson(doc,text,DeserializationOption::NestingLimit(depth))){error="printer_invalid_model_or_limit";return false;}return true;
}
// A malformed/huge printer model cannot grow the parser without bound.
class PrinterJsonAllocator final:public ArduinoJson::Allocator {
    struct alignas(std::max_align_t) Header{size_t size;};size_t used_=0;
public:
    void* allocate(size_t n)override{if(n>65536-used_)return nullptr;
        auto* p=static_cast<Header*>(heap_caps_malloc(sizeof(Header)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!p)return nullptr;p->size=n;used_+=n;return p+1;}
    void deallocate(void* p)override{if(!p)return;auto* h=static_cast<Header*>(p)-1;used_-=h->size;heap_caps_free(h);}
    void* reallocate(void* p,size_t n)override{if(!p)return allocate(n);auto* h=static_cast<Header*>(p)-1;
        if(n>65536-(used_-h->size))return nullptr;const size_t old=h->size;
        auto* q=static_cast<Header*>(heap_caps_realloc(h,sizeof(Header)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!q)return nullptr;used_=used_-old+n;q->size=n;return q+1;}
};
}
