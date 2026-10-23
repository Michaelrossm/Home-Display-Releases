#pragma once
#include "../../core/AppManager.h"
#include "WebAppManager.h"

namespace hp {

class BrowserApp : public IApp {
public:
    const char* id() const override { return "browser"; }
    const char* name() const override { return "Browser"; }
    const char* icon() const override { return LV_SYMBOL_DIRECTORY; }

    bool begin() override;
    void open(lv_obj_t* parent) override;
    void close() override { if(pollTimer_){lv_timer_delete(pollTimer_);pollTimer_=nullptr;} root_ = nullptr; }

    void openSaved(size_t index);

private:
    static void reloadCb(lv_event_t* e);
    static void saveCb(lv_event_t* e);
    static void pollCb(lv_timer_t* t);
    void render(const char* url);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* urlLabel_ = nullptr;
    String currentUrl_;
    String currentTitle_;
    lv_timer_t* pollTimer_ = nullptr;
};

}
