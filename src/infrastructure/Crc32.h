#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hp { uint32_t crc32(const void* data,size_t size,uint32_t seed=0xffffffffu); }
