#pragma once
#include <Arduino.h>

namespace hp {

// Minimal ZIP reader (store + deflate methods, no zip64) that installs a Lua
// app package (manifest.json + its entry script) onto the AppFS partition.
// Only Lua app packages are supported this way: native C++ apps cannot be
// loaded without a firmware flash, so this is intentionally scoped to that.
class ZipInstaller {
public:
    static bool installFromZip(const uint8_t* data, size_t len, String& outId, String& error);
};

}
