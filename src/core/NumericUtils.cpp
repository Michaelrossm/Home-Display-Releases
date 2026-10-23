#include "NumericUtils.h"
#include <cmath>
#include <cstdio>

namespace hp::numeric {
namespace {
constexpr double kDivisionEpsilon = 1e-15;

double scaleFor(uint8_t decimals) {
    static constexpr double scales[] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0};
    return scales[decimals <= 6 ? decimals : 6];
}
}

bool calculate(char operation, double left, double right, double& result) {
    if (!std::isfinite(left) || !std::isfinite(right)) return false;
    switch (operation) {
        case '+': result = left + right; break;
        case '-': result = left - right; break;
        case 'x': case '*': result = left * right; break;
        case '/':
            if (std::fabs(right) < kDivisionEpsilon) return false;
            result = left / right;
            break;
        default: return false;
    }
    return std::isfinite(result);
}

double roundTo(double value, uint8_t decimals) {
    if (!std::isfinite(value)) return NAN;
    const double scale = scaleFor(decimals);
    const double rounded = std::round(value * scale) / scale;
    return std::isfinite(rounded) ? rounded : NAN;
}

String fixed(double value, uint8_t decimals, const char* fallback) {
    const double rounded = roundTo(value, decimals);
    return std::isfinite(rounded) ? String(rounded, static_cast<unsigned int>(decimals)) : String(fallback ? fallback : "-");
}

bool general(double value, char* target, size_t targetSize) {
    if (!target || targetSize < 2 || !std::isfinite(value)) return false;
    const int written = snprintf(target, targetSize, "%.12g", value);
    return written >= 0 && static_cast<size_t>(written) < targetSize;
}

int percentOf(int value, int maximum) {
    if (maximum <= 0) return 0;
    const double percent = (static_cast<double>(value) * 100.0) / maximum;
    return constrain(static_cast<int>(std::lround(percent)), 0, 100);
}

int percentToValueRounded(int percent, int maximum) {
    if (maximum <= 0) return 0;
    return constrain(static_cast<int>(std::lround(static_cast<double>(constrain(percent, 0, 100)) * maximum / 100.0)), 0, maximum);
}

int percentToValueFloor(int percent, int maximum) {
    if (maximum <= 0) return 0;
    return constrain((constrain(percent, 0, 100) * maximum) / 100, 0, maximum);
}

}
