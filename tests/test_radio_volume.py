from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class RadioVolumeTests(unittest.TestCase):
    def test_only_volume_targets_coalesce_without_replaying_commands(self):
        source=(ROOT/'src/apps/radio/RadioService.cpp').read_text()
        queue=source[source.index('bool RadioService::queue('):source.index('int RadioService::copyModes(')]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cassert>
constexpr int pdTRUE=1;int xSemaphoreTake(void*,int){return 1;}void xSemaphoreGive(void*){}int pdMS_TO_TICKS(int n){return n;}
namespace hp {
enum class MediaCommand{Next,Previous,PowerToggle,Stop};
struct SecurityService{bool locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}};
struct RadioSettings{struct Config{bool parentalEnabled=false,lockDisplayControls=false,lockSources=false,lockPowerOff=false;};
 static RadioSettings& instance(){static RadioSettings s;return s;}Config load(){return {};}};
struct RadioService{enum class Request{None,Command,Volume,Modes,Presets,Discover,Reconfigure,SelectMode,SelectPreset,BrowseSelect,BrowseBack};
 void* mux_=reinterpret_cast<void*>(1);Request pending_=Request::None;int pendingValue_=0;bool catalogBusy_=false;
 struct State{bool power=true;};State snapshot(){return {};}
 bool queue(Request,int);
};
'''+queue+r'''
}
int main(){using namespace hp;using R=RadioService::Request;RadioService radio;
 assert(radio.queue(R::Volume,35)&&radio.queue(R::Volume,36)&&radio.queue(R::Volume,37));assert(radio.pendingValue_==37);
 assert(!radio.queue(R::Command,1)&&radio.pending_==R::Volume&&radio.pendingValue_==37);
 radio.pending_=R::None;assert(radio.queue(R::Command,2));assert(!radio.queue(R::Volume,38)&&radio.pendingValue_==2);
 radio.pending_=R::None;radio.catalogBusy_=true;assert(!radio.queue(R::Volume,38));radio.catalogBusy_=false;
 SecurityService::instance().locked=true;assert(!radio.queue(R::Volume,38));
}
''')
