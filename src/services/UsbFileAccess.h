#pragma once
#include <Arduino.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <vector>
namespace hp::usbfiles {
struct Entry {String name;uint64_t bytes;bool directory;};
bool list(const String& relative,std::vector<Entry>& out,const std::atomic<bool>* cancel=nullptr);
bool stat(const String& relative,uint64_t& bytes,bool& directory);
bool read(const String& relative,uint64_t offset,uint8_t* dst,size_t capacity,size_t& got,uint64_t& total,const std::atomic<bool>* cancel=nullptr);
}
