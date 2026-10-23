#pragma once
#include "../../core/AppManager.h"
#include "../../services/IntercomService.h"
#include <atomic>
namespace hp {
struct IntercomPage;
class IntercomApp final : public IApp {
public:
    const char* id() const override {return "intercom";}
    const char* name() const override {return "Sprechanlage";}
    const char* icon() const override {return "@intercom";}
    const char* version() const override {return "1.0.0";}
    AppKind kind() const override {return AppKind::Native;}
    bool userInstallable() const override {return true;}
    bool enabledByDefault() const override {return true;}
    bool launcherVisible() const override {return true;}
    bool hasSettings() const override {return true;}
    const char* webPath() const override {return "/intercom";}
    AppPermissions permissions() const override {
        return perm(AppPermission::Network)|perm(AppPermission::Camera)|perm(AppPermission::Intercom)|
               perm(AppPermission::Audio)|perm(AppPermission::Microphone)|perm(AppPermission::DoorOpen);
    }
    bool begin() override {return true;}
    void open(lv_obj_t*) override;
    bool openSettings(lv_obj_t*) override;
    void close() override;
private:
    friend struct IntercomPage;
    enum class Operation {Load,Save,Remove,Start,Diagnose};
    struct Work {
        Operation operation=Operation::Load;
        IntercomDevice device{}, devices[5]{};
        size_t count=0;
        char password[257]{}, error[65]{};
        char diagnostics[2305]{};
        bool ok=false;
        uint32_t generation=0;
    } work_;
    std::atomic<bool> busy_{false},done_{false};
    std::atomic<uint32_t> generation_{0};
    IntercomPage* page_=nullptr;
    void create(lv_obj_t*,bool);
    bool queue(Operation,const IntercomDevice* =nullptr,const char* password=nullptr);
    static void worker(void*);
};
}
