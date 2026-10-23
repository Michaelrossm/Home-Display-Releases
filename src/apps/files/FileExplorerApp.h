#pragma once
#include "../../core/AppManager.h"
#include <atomic>
namespace hp {
struct FileExplorerPage;
class FileExplorerApp:public IApp {
public:
    const char* id()const override{return "files";}
    const char* webPath()const override{return "/files";}
    const char* name()const override{return "Dateien";}
    const char* icon()const override{return "@files";}
    // Native app remains non-removable, but participates in the launcher.
    bool launcherVisible()const override{return true;}
    bool begin()override{return true;}
    AppKind kind()const override{return AppKind::Native;}
    void open(lv_obj_t* parent)override;
    void close()override;
    static void requestWallpaperPicker(){picker_=true;}
private:
    FileExplorerPage* page_=nullptr;static std::atomic<bool> picker_;
};
}
