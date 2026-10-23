#pragma once

// The rest of the firmware remains size-optimized. This bounded hot loop is
// compiled for throughput by GCC; native test compilers use their defaults.
#if defined(__GNUC__) && !defined(__clang__)
#define HOME_DISPLAY_ROTATE_OPT __attribute__((optimize("O2")))
#else
#define HOME_DISPLAY_ROTATE_OPT
#endif

// Included after the pinned adapter's common header. Keep its generic fallback
// for other formats/rotations; specialize only the board's RGB565 quarter-turn.
static void IRAM_ATTR HOME_DISPLAY_ROTATE_OPT home_display_rotate_copy_region(
    const void *src, void *dst, uint16_t x1, uint16_t y1,
    uint16_t x2, uint16_t y2, uint16_t stride, uint16_t physical_width,
    uint16_t physical_height, esp_lv_adapter_rotation_t rotation,
    uint8_t color_bytes, int block_small, int block_large)
{
    if (color_bytes != 2 ||
        (rotation != ESP_LV_ADAPTER_ROTATE_90 && rotation != ESP_LV_ADAPTER_ROTATE_270)) {
        display_rotate_copy_region(src, dst, x1, y1, x2, y2, stride,
                                  physical_width, physical_height, rotation,
                                  color_bytes, block_small, block_large);
        return;
    }

    const uint16_t *input = (const uint16_t *)src;
    uint16_t *output = (uint16_t *)dst;
    const int width = x2 - x1 + 1;
    const int height = y2 - y1 + 1;
    const bool clockwise = rotation == ESP_LV_ADAPTER_ROTATE_90;

    // Small tiles keep both strided source rows and destination rows cached.
    // Write each destination row consecutively, rather than jumping 1600 bytes
    // between every pixel. No scratch buffer, allocations or cache-policy change.
    for (int top = 0; top < height; top += 16) {
        const int rows = height - top < 16 ? height - top : 16;
        for (int left = 0; left < width; left += 16) {
            const int end = width - left < 16 ? width : left + 16;
            for (int x = left; x < end; ++x) {
                const uint16_t *column = input + (size_t)top * stride + x;
                if (clockwise) {
                    uint16_t *row = output + (size_t)(x1 + x) * physical_width
                                    + physical_width - 1 - (y1 + top);
                    for (int y = 0; y < rows; ++y) {
                        row[-y] = column[(size_t)y * stride];
                    }
                } else {
                    uint16_t *row = output + (size_t)(physical_height - 1 - (x1 + x))
                                    * physical_width + y1 + top;
                    for (int y = 0; y < rows; ++y) {
                        row[y] = column[(size_t)y * stride];
                    }
                }
            }
        }
    }
}
#undef HOME_DISPLAY_ROTATE_OPT
