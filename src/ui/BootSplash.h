#pragma once
namespace hp {
class BootSplash final {
public:
    // UI-owned static bitmap, never a rendering task.
    static bool show();
    static void hide();
};
}
