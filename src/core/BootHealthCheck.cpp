#include "BootHealthCheck.h"
#include "BootManager.h"
#include "SystemSupervisor.h"
#include "../services/ConfigService.h"
#include "../services/StorageService.h"
#include "../infrastructure/AssetManager.h"
#include <esp_heap_caps.h>
namespace hp {
BootHealthCheck& BootHealthCheck::instance(){static BootHealthCheck h;return h;}
void BootHealthCheck::reportUiReady(){ui_=true;}void BootHealthCheck::reportFrame(){++frames_;}void BootHealthCheck::reportTouchReady(bool r){touch_=r;}void BootHealthCheck::reportEventLoop(){lastEventLoop_=millis();}
BootHealthReport BootHealthCheck::evaluate()const{BootHealthReport r;r.config=ConfigService::instance().status()!=ConfigStatus::Error&&!ConfigService::instance().newerSchema();r.systemData=StorageService::instance().internalReady();r.appFs=AssetManager::instance().appFsAvailable();r.ui=ui_;r.frames=frames_>1;r.touch=touch_;r.eventLoop=lastEventLoop_&&(millis()-lastEventLoop_<1000);r.supervisor=SystemSupervisor::instance().healthyCritical();r.freeInternalHeap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);r.heap=r.freeInternalHeap>=128*1024;r.bootloop=!BootManager::instance().state().safeMode;const struct{bool ok;const char*name;}c[]={{r.config,"config"},{r.systemData,"systemdata"},{r.appFs,"appfs"},{r.ui,"ui"},{r.frames,"frames"},{r.touch,"touch"},{r.eventLoop,"eventloop"},{r.supervisor,"supervisor"},{r.heap,"heap"},{r.bootloop,"bootloop"}};for(auto&x:c)if(!x.ok){r.firstFailure=x.name;break;}return r;}
}
