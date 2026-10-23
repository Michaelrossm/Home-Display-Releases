#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace hp {

class HomeReorderController {
public:
    static HomeReorderController& instance();
    void loop();
    bool requestDockChange(const char* id, bool remove);
    bool dragging() const { return dragging_!=nullptr; }

private:
    HomeReorderController();

    struct Binding {
        lv_obj_t* button = nullptr;
        lv_obj_t* overlay = nullptr;
        char id[40]{};
        bool web = false;
        size_t backendIndex = SIZE_MAX;
    };

    static constexpr size_t kMaxVisible = 10;
    Binding bindings_[kMaxVisible]{};
    size_t bindingCount_ = 0;
    lv_obj_t* boundGrid_ = nullptr;
    lv_obj_t* boundFirst_ = nullptr;
    uint32_t boundRevision_=0;
    Binding* dragging_ = nullptr;
    size_t dragSourceIndex_ = SIZE_MAX;
    uint32_t suppressClicksUntil_ = 0;
    uint32_t lastScanMs_ = 0;

    char pendingDragged_[40]{};
    char pendingTarget_[40]{};
    bool pendingAfter_ = false;
    int8_t pendingPageDirection_=0;
    std::atomic<bool> pendingMove_{false};
    struct DockChange { char id[40]{}; bool remove=false; };
    StaticQueue_t dockQueueStorage_{};
    uint8_t dockQueueBytes_[4*sizeof(DockChange)]{};
    QueueHandle_t dockQueue_=nullptr;

    static void tileEvent(lv_event_t* e);

    void scanAndBind();
    void bindGrid(lv_obj_t* grid);
    lv_obj_t* findGrid() const;
    void launch(const Binding& binding);
    void beginDrag(Binding& binding);
    void updateDrag();
    void finishDrag(Binding& binding);
    void cancelDrag(Binding& binding);
    size_t childIndex(lv_obj_t* obj) const;
    Binding* bindingForButton(lv_obj_t* button);
};

} // namespace hp
