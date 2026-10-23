#pragma once
#include "../../core/AppManager.h"
namespace hp {
struct PhotoPage;
class PhotoApp final:public IApp {
public:
    const char* id()const override{return "photos";}
    const char* name()const override{return "Fotos";}
    const char* icon()const override{return "@photos";}
    const char* version()const override{return "1.0.0";}
    bool launcherVisible()const override{return true;}
    AppKind kind()const override{return AppKind::Native;}
    const char* webPath()const override{return "/photos";}
    bool begin()override;
    void open(lv_obj_t*)override;
    void close()override;
    void setLaunchArguments(const String& arguments)override;
private:
    PhotoPage* page_=nullptr;bool autoFrame_=false,manualFrame_=false;
};
}
