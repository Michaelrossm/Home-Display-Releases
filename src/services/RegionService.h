#pragma once
#include <Arduino.h>
#include <atomic>
namespace hp {
class RegionService {
public:
    static RegionService& instance();
    void begin(); // Boot-only config read; consumers use the cached index.
    static size_t count();
    static const char* code(size_t);
    static const char* name(size_t);
    static int find(const char*); // ISO code or existing localized country name.
    size_t selected()const{return selected_.load();}
    const char* country()const{return code(selected());}
    bool request(size_t); // Persist via existing local worker, never under UI lock.
    bool busy()const{return pending_.load()!=UINT16_MAX;}
    const char* status()const;
    String catalogJson()const;
private:
    std::atomic<uint16_t> selected_{0},pending_{UINT16_MAX};
    std::atomic<uint8_t> state_{0};
    static void save(void*);
};
}
