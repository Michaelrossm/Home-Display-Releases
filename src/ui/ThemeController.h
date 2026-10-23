#pragma once
#include <cstdint>
namespace hp {
class ThemeController {
public:
    static ThemeController& instance();
    bool apply(uint8_t mode, uint8_t accent);
private:
    ThemeController() = default;
};
}
