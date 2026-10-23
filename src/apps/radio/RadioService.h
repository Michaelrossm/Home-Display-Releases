#pragma once
#include "../../core/BackgroundScheduler.h"
#include "../../services/MediaSessionService.h"
#include "engine/RadioClient.h"
#include "RadioDiscovery.h"
#include <freertos/semphr.h>
namespace hp {
class RadioService:public IBackgroundJob {
public:
    static RadioService& instance(); bool begin(); void backgroundTick()override;
    BackgroundJobClass backgroundClass() const override { return BackgroundJobClass::Blocking; }
    uint32_t backgroundIntervalMs() const override { return 200UL; }
    const char* backgroundName() const override { return "radio"; }
    RadioState snapshot(uint32_t waitMs=20, bool* valid=nullptr); bool command(MediaCommand c); bool setVolume(int value);
    bool requestModes(); bool requestSelectMode(int key); bool requestPresets(); bool requestSelectPreset(int key); bool requestBrowseRoot();
    bool requestBrowseSelect(size_t index); bool requestBrowseBack();
    bool requestReconfigure();
    bool requestAutoDiscover(); int copyDiscovered(RadioDiscoveryDevice* target,int capacity,uint32_t waitMs=20); String discoveryStatus(uint32_t waitMs=20);
    int copyModes(RadioMode* target, int capacity,uint32_t waitMs=20); int copyPresets(RadioPreset* target, int capacity,uint32_t waitMs=20);
    int copyBrowse(RadioNavItem* target, int capacity,uint32_t waitMs=20); bool catalogBusy(uint32_t waitMs=5); uint32_t catalogRevision(uint32_t waitMs=5);
private:
    enum class Request:uint8_t { None, Command, Volume, Modes, SelectMode, Presets, SelectPreset, BrowseRoot, BrowseSelect, BrowseBack, Reconfigure, Discover };
    bool queue(Request request, int value=0); void updateCatalog(Request request, int value);
    void executeCommand(MediaCommand command);
    RadioClient client_; RadioState state_; SemaphoreHandle_t mux_=nullptr; uint32_t last_=0, catalogRevision_=0;
    uint32_t mediaSourceId_=0; Request pending_=Request::None; int pendingValue_=0;
    bool catalogBusy_=false; RadioMode modes_[16]; int modeCount_=0; RadioPreset presets_[32]; int presetCount_=0;
    bool reconfigurePending_=false;
    String commandError_;
    RadioNavItem browse_[48]; int browseCount_=0; static bool mediaThunk(MediaCommand c);
    RadioDiscoveryDevice discovered_[8]; int discoveredCount_=0; String discoveryStatus_;
};
}
