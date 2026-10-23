#pragma once

#include <esp_heap_caps.h>
#include <new>

namespace hp {

// Allocate long lived tables outside internal RAM when PSRAM is available.
// The internal heap fallback preserves operation on builds without PSRAM.
template <typename T>
T* allocatePsramArray(size_t count) {
    if (!count || count > SIZE_MAX / sizeof(T)) return nullptr;

    const size_t bytes = count * sizeof(T);
    void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;

    T* items = static_cast<T*>(memory);
    for (size_t i = 0; i < count; ++i) new (&items[i]) T{};
    return items;
}

}
