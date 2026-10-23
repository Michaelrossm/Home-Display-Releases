#pragma once
#include "../../core/AppManager.h"
namespace hp {
struct MusicPage;
class MusicApp final:public IApp {
public:
 const char* id()const override{return "music";}const char* name()const override{return "Musik";}
 const char* icon()const override{return "@music";}const char* version()const override{return "1.0.0";}
 AppKind kind()const override{return AppKind::Native;}bool launcherVisible()const override{return true;}
 bool begin()override{return true;}void open(lv_obj_t*)override;void close()override;
private:MusicPage* page_=nullptr;
};
}
