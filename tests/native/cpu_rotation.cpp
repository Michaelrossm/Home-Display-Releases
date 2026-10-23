#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#define IRAM_ATTR
enum esp_lv_adapter_rotation_t {
    ESP_LV_ADAPTER_ROTATE_0, ESP_LV_ADAPTER_ROTATE_90,
    ESP_LV_ADAPTER_ROTATE_180, ESP_LV_ADAPTER_ROTATE_270
};
static int fallback_calls;
static void display_rotate_copy_region(const void *, void *, uint16_t, uint16_t,
    uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
    esp_lv_adapter_rotation_t, uint8_t, int, int) { ++fallback_calls; }
#include "../../cmake/cpu_rotation.h"

static void check(int pw, int ph, int x1, int y1, int width, int height,
                  int padding, esp_lv_adapter_rotation_t rotation)
{
    const int stride = width + padding;
    std::vector<uint16_t> source(stride * height);
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint16_t(i * 17 + 3);
    // Guard words and unchanged surroundings detect out-of-region writes.
    std::vector<uint16_t> actual(pw * ph + 2, 0xa55a), expected = actual;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int dx = rotation == ESP_LV_ADAPTER_ROTATE_90 ? pw - 1 - (y1 + y) : y1 + y;
            const int dy = rotation == ESP_LV_ADAPTER_ROTATE_90 ? x1 + x : ph - 1 - (x1 + x);
            expected[1 + dy * pw + dx] = source[y * stride + x];
        }
    }
    home_display_rotate_copy_region(source.data(), actual.data() + 1,
        x1, y1, x1 + width - 1, y1 + height - 1, stride, pw, ph,
        rotation, 2, 64, 512);
    assert(actual == expected);
}

int main()
{
    for (auto rotation : {ESP_LV_ADAPTER_ROTATE_90, ESP_LV_ADAPTER_ROTATE_270}) {
        for (int width : {1, 2, 15, 16, 17, 31, 32, 33}) {
            for (int height : {1, 2, 15, 16, 17, 31, 32, 33}) {
                for (int padding : {0, 1, 7}) {
                    check(80, 96, 0, 0, width, height, padding, rotation);
                    check(80, 96, 96 - width, 80 - height, width, height, padding, rotation);
                    check(80, 96, 7, 9, width, height, padding, rotation);
                }
            }
        }
        check(800, 1280, 0, 0, 1280, 800, 0, rotation);
        check(800, 1280, 0, 0, 1280, 50, 0, rotation);
        check(800, 1280, 13, 729, 1231, 71, 1, rotation);
    }
    assert(fallback_calls == 0);
    uint16_t pixel = 0;
    for (auto rotation : {ESP_LV_ADAPTER_ROTATE_0, ESP_LV_ADAPTER_ROTATE_180})
        home_display_rotate_copy_region(&pixel, &pixel, 0, 0, 0, 0, 1, 1, 1, rotation, 2, 64, 512);
    home_display_rotate_copy_region(&pixel, &pixel, 0, 0, 0, 0, 1, 1, 1,
        ESP_LV_ADAPTER_ROTATE_90, 3, 64, 512);
    assert(fallback_calls == 3);
}
