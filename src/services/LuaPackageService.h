#pragma once
#include <Arduino.h>

namespace hp {

struct LuaPackageRemoveResult {
    bool ok = false;
    bool packageRemoved = false;
    bool dataPurged = false;
    String error;
};

class LuaPackageService {
public:
    static LuaPackageService& instance();
    LuaPackageRemoveResult remove(const String& id, bool purgeData);

private:
    LuaPackageService() = default;
};

} // namespace hp
