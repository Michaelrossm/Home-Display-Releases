#pragma once
#include <Arduino.h>
namespace hp {
struct BootHealthReport{bool config=false,systemData=false,appFs=false,ui=false,frames=false,touch=false,eventLoop=false,supervisor=false,heap=false,bootloop=false;uint32_t freeInternalHeap=0;String firstFailure;bool ok()const{return config&&systemData&&appFs&&ui&&frames&&touch&&eventLoop&&supervisor&&heap&&bootloop;}};
class BootHealthCheck{public:static BootHealthCheck&instance();void reportUiReady();void reportFrame();void reportTouchReady(bool);void reportEventLoop();BootHealthReport evaluate()const;private:bool ui_=false,touch_=false;uint32_t frames_=0,lastEventLoop_=0;};
}
