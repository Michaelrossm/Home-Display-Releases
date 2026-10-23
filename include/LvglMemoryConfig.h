#pragma once

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

// The BSP calls lv_init(). Reserve its one TLSF pool in PSRAM before any
// widgets are created; never consume DMA-capable internal SRAM for it.
// Four retained Lua views plus render scratch must not share the 64 KiB
// Kconfig default. Keep the reservation bounded, independent of free heap.
#define LV_MEM_SIZE (4U * 1024U * 1024U)
#define LV_MEM_POOL_ALLOC(size) hp_lvgl_pool_alloc(size)

static inline void* hp_lvgl_pool_alloc(size_t size)
{
    void* pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pool) {
        ESP_LOGE("LVGL_MEMORY", "PSRAM pool allocation failed (%u bytes)", (unsigned)size);
        abort();
    }
    ESP_LOGI("LVGL_MEMORY", "Reserved %u bytes in PSRAM", (unsigned)size);
    return pool;
}

// The default while(1) assertion leaves the panel scanning an old frame and
// starves IDLE0 forever. Preserve IDF's panic/coredump and BootHealth handling.
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();
