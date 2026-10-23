#pragma once
#include "LuaScriptReader.h"
#include <atomic>
#include <memory>

namespace hp {
// Worker owns a snapshot of package identity, never a LuaApp or LVGL pointer.
// Shared lifetime lets uninstall/navigation cancel without waiting for I/O.
struct LuaScriptPreparation {
    String folder,id,fallback;
    bool appFs=true,usb=false,settings=false;
    uint32_t usbGeneration=0;
    LuaScriptBuffer script;
    const char* error=nullptr;
    std::atomic<bool> cancelled{false},ready{false};
    static bool enqueue(const std::shared_ptr<LuaScriptPreparation>&);
private:
    static void run(void*);
};
}
