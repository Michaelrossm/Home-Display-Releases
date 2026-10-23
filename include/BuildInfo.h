#pragma once
#include <stdint.h>

namespace hp::build {
inline constexpr const char* kProduct = "Home-Display";
inline constexpr const char* kVersion = "0.1.0";
inline constexpr uint32_t kConfigSchema = 2;
inline constexpr uint32_t kUpdateManifestSchema = 1;
inline constexpr uint32_t kAssetManifestSchema = 1;
#if defined(HOMEDISPLAY_PRODUCTION)
inline constexpr bool kProduction = true;
inline constexpr const char* kProfile = "production";
#else
inline constexpr bool kProduction = false;
inline constexpr const char* kProfile = "development";
#endif
}
