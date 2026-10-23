#include "HomeReorderController.h"
#include "UiShell.h"
#include "UiTheme.h"
#include "../hardware/DisplayManager.h"
#include "../core/AppManager.h"
#include "../core/AppRegistry.h"
#include "../apps/browser/BrowserApp.h"
#include "../services/HomeAppOrderService.h"
#include "../services/DockShortcutService.h"
#include "../services/IdleService.h"
#include "../services/SecurityService.h"
#include "../core/EventLog.h"
#include <cstring>

namespace hp {
namespace {


bool pointInside(const lv_area_t& a, const lv_point_t& p) {
    return p.x >= a.x1 && p.x <= a.x2 &&
           p.y >= a.y1 && p.y <= a.y2;
}

} // namespace

HomeReorderController& HomeReorderController::instance() {
    static HomeReorderController c;
    return c;
}

HomeReorderController::HomeReorderController() {
    dockQueue_=xQueueCreateStatic(4,sizeof(DockChange),dockQueueBytes_,&dockQueueStorage_);
}
bool HomeReorderController::requestDockChange(const char* id,bool remove) {
    if(!id||!id[0]||strlen(id)>=sizeof(DockChange::id)||SecurityService::instance().tabletLocked())return false;
    DockChange change;strlcpy(change.id,id,sizeof(change.id));change.remove=remove;
    return dockQueue_&&xQueueSend(dockQueue_,&change,0)==pdPASS;
}

size_t HomeReorderController::childIndex(lv_obj_t* obj) const {
    if (!boundGrid_ || !obj) return SIZE_MAX;
    const uint32_t n = lv_obj_get_child_count(boundGrid_);
    for (uint32_t i = 0; i < n; ++i)
        if (lv_obj_get_child(boundGrid_, i) == obj)
            return i;
    return SIZE_MAX;
}

HomeReorderController::Binding*
HomeReorderController::bindingForButton(lv_obj_t* button) {
    for (size_t i = 0; i < bindingCount_; ++i)
        if (bindings_[i].button == button)
            return &bindings_[i];
    return nullptr;
}

lv_obj_t* HomeReorderController::findGrid() const {
    if (AppManager::instance().current()) return nullptr;

    lv_obj_t* content = UiShell::instance().homeContent();
    if (!content) return nullptr;

    const uint32_t direct = lv_obj_get_child_count(content);
    for (uint32_t i = 0; i < direct; ++i) {
        lv_obj_t* candidate = lv_obj_get_child(content, i);
        if (!candidate) continue;

        const uint32_t children = lv_obj_get_child_count(candidate);
        if (!children || children > kMaxVisible) continue;

        lv_obj_t* first = lv_obj_get_child(candidate, 0);
        if (!first) continue;

        // Launcher tiles are currently 160x160; this deliberately distinguishes
        // them from the 68px dock buttons without depending on app names.
        if (lv_obj_get_width(first) == 160 &&
            lv_obj_get_height(first) == 160)
            return candidate;
    }
    return nullptr;
}

void HomeReorderController::bindGrid(lv_obj_t* grid) {
    if (!grid) return;

    const size_t count =
        min<size_t>(lv_obj_get_child_count(grid), kMaxVisible);
    if (!count) return;

    auto& registry = AppRegistry::instance();
    bindingCount_ = count;
    for (size_t i = 0; i < count; ++i) {
        Binding& b = bindings_[i];
        b = {};
        b.button = lv_obj_get_child(grid, i);
        const auto* id = static_cast<const char*>(lv_obj_get_user_data(b.button));
        AppRegistryEntry entry;
        if (!id || !registry.lookup(id, entry) ||
            entry.state != AppRegistryState::Enabled) {
            bindingCount_ = i;
            break;
        }

        strlcpy(
            b.id, entry.id.c_str(), sizeof(b.id));
        b.web = entry.kind == AppKind::Web;
        b.backendIndex = entry.backendIndex;

        b.overlay = lv_obj_create(b.button);
        lv_obj_remove_style_all(b.overlay);
        lv_obj_set_size(
            b.overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_align(
            b.overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(
            b.overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(
            b.overlay, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_clear_flag(
            b.overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            b.overlay, tileEvent, LV_EVENT_ALL, &b);
    }

    boundGrid_ = grid;
    boundFirst_ = lv_obj_get_child(grid, 0);
    boundRevision_=UiShell::instance().homeRevision();
}

void HomeReorderController::scanAndBind() {
    // The persistent desktop still owns these overlays while an app is open.
    // Keep bindings instead of creating duplicates on every return to Home.
    if(AppManager::instance().current()){
        if(dragging_&&boundRevision_==UiShell::instance().homeRevision())cancelDrag(*dragging_);
        dragging_=nullptr;return;
    }
    lv_obj_t* grid = findGrid();
    if (!grid) {
        boundGrid_ = nullptr;
        boundFirst_ = nullptr;
        bindingCount_ = 0;
        dragging_ = nullptr;
        return;
    }

    lv_obj_t* first =
        lv_obj_get_child_count(grid)
            ? lv_obj_get_child(grid, 0)
            : nullptr;

    const bool sameRevision=boundRevision_==UiShell::instance().homeRevision();
    if (sameRevision && grid == boundGrid_ && first == boundFirst_)
        return;

    // A reorder changes the child order but keeps the same grid alive.
    // Remove our old overlays before rebinding so callbacks never stack up.
    if (sameRevision && grid == boundGrid_) {
        for (size_t i = 0; i < bindingCount_; ++i) {
            if (bindings_[i].overlay &&
                lv_obj_is_valid(bindings_[i].overlay))
                lv_obj_delete(bindings_[i].overlay);
        }
    }

    boundGrid_ = nullptr;
    boundFirst_ = nullptr;
    bindingCount_ = 0;
    dragging_ = nullptr;
    bindGrid(grid);
}

void HomeReorderController::launch(const Binding& binding) {
    if (SecurityService::instance().tabletLocked()) return;

    IdleService::instance().activity();

    UiShell::instance().requestLauncherOpen(binding.id);
}

void HomeReorderController::beginDrag(Binding& binding) {
    if(dragging_)return;
    if (!boundGrid_ || SecurityService::instance().tabletLocked())
        return;

    const size_t source = childIndex(binding.button);
    if (source == SIZE_MAX) return;
    UiShell::instance().setDesktopEditing(true);

    dragging_ = &binding;
    dragSourceIndex_ = source;
    suppressClicksUntil_ = millis() + 800;

    lv_area_t buttonArea{}, gridArea{};
    lv_obj_get_coords(binding.button, &buttonArea);
    lv_obj_get_coords(boundGrid_, &gridArea);

    lv_obj_add_flag(binding.button, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(boundGrid_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_move_foreground(boundGrid_);
    lv_obj_set_pos(
        binding.button,
        buttonArea.x1 - gridArea.x1,
        buttonArea.y1 - gridArea.y1);

    lv_obj_set_style_outline_width(binding.button, 3, 0);
    lv_obj_set_style_outline_color(
        binding.button, UiTheme::instance().accent(), 0);
    lv_obj_set_style_opa(binding.button, LV_OPA_80, 0);

    IdleService::instance().activity();
}

void HomeReorderController::updateDrag() {
    if (!dragging_ || !boundGrid_) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p{};
    lv_indev_get_point(indev, &p);

    lv_area_t gridArea{};
    lv_obj_get_coords(boundGrid_, &gridArea);

    const int32_t w = lv_obj_get_width(dragging_->button);
    const int32_t h = lv_obj_get_height(dragging_->button);
    lv_obj_set_pos(
        dragging_->button,
        p.x - gridArea.x1 - w / 2,
        p.y - gridArea.y1 - h / 2);

    IdleService::instance().activity();
}

void HomeReorderController::finishDrag(Binding& binding) {
    if (&binding != dragging_ || !boundGrid_) return;

    lv_indev_t* indev = lv_indev_active();
    lv_point_t p{};
    if (indev) lv_indev_get_point(indev, &p);

    lv_area_t dockArea{},gridArea{};
    auto* dock=UiShell::instance().dock();
    if(dock)lv_obj_get_coords(dock,&dockArea);
    lv_obj_get_coords(boundGrid_,&gridArea);
    if(indev&&dock&&pointInside(dockArea,p)) {
        if(!requestDockChange(binding.id,false))
            EventLog::instance().add("WARN","DOCK","Schnellleisten-Anfrage konnte nicht eingereiht werden");
        cancelDrag(binding);
        return;
    }
    // Drop at the lateral page controls. Keep the current objects alive until
    // finger-up; only the existing main loop persists and replaces the page.
    lv_area_t homeArea{};lv_obj_get_coords(UiShell::instance().homeContent(),&homeArea);
    const int8_t direction=p.x<homeArea.x1+64?-1:p.x>homeArea.x2-64?1:0;
    if(indev&&direction&&p.y>=gridArea.y1&&p.y<=gridArea.y2&&!SecurityService::instance().tabletLocked()){
        strlcpy(pendingDragged_,binding.id,sizeof(pendingDragged_));pendingTarget_[0]=0;
        pendingPageDirection_=direction;cancelDrag(binding);
        pendingMove_.store(true,std::memory_order_release);return;
    }
    if(!indev||!pointInside(gridArea,p)||SecurityService::instance().tabletLocked()) {
        cancelDrag(binding);return;
    }

    size_t targetIndex = dragSourceIndex_;
    int64_t bestDistance = INT64_MAX;

    for (size_t i = 0; i < bindingCount_; ++i) {
        if (bindings_[i].button == binding.button) continue;

        lv_area_t a{};
        lv_obj_get_coords(bindings_[i].button, &a);
        const int32_t cx = (a.x1 + a.x2) / 2;
        const int32_t cy = (a.y1 + a.y2) / 2;
        const int64_t dx = static_cast<int64_t>(p.x) - cx;
        const int64_t dy = static_cast<int64_t>(p.y) - cy;
        const int64_t d = dx * dx + dy * dy;
        if (d < bestDistance) {
            bestDistance = d;
            targetIndex = childIndex(bindings_[i].button);
        }
    }

    Binding* targetBinding = nullptr;
    if (targetIndex != SIZE_MAX &&
        targetIndex != dragSourceIndex_) {
        lv_obj_t* targetObj =
            lv_obj_get_child(boundGrid_, targetIndex);
        targetBinding = bindingForButton(targetObj);
    }

    lv_obj_clear_flag(binding.button, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(boundGrid_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_outline_width(binding.button, 0, 0);
    lv_obj_set_style_opa(binding.button, LV_OPA_COVER, 0);

    if (targetBinding &&
        strcmp(binding.id, targetBinding->id) != 0) {
        const bool after = dragSourceIndex_ < targetIndex;

        strlcpy(
            pendingDragged_, binding.id,
            sizeof(pendingDragged_));
        strlcpy(
            pendingTarget_, targetBinding->id,
            sizeof(pendingTarget_));
        pendingAfter_ = after;
        pendingPageDirection_=0;
        pendingMove_.store(true, std::memory_order_release);

        // Immediate visual feedback. Persistent order is written later from
        // loop(), outside this LVGL input callback.
        lv_obj_move_to_index(
            binding.button,
            static_cast<int32_t>(targetIndex));
        lv_obj_update_layout(boundGrid_);
    } else {
        // Let flex layout put the tile back into its original slot.
        lv_obj_update_layout(boundGrid_);
    }

    dragging_ = nullptr;
    dragSourceIndex_ = SIZE_MAX;
    suppressClicksUntil_ = millis() + 500;

    // Rebind after flex has settled, because child order may have changed.
    boundFirst_ = nullptr;
}

void HomeReorderController::cancelDrag(Binding& binding) {
    if (&binding != dragging_) return;
    lv_obj_clear_flag(binding.button, LV_OBJ_FLAG_FLOATING);
    if(boundGrid_)lv_obj_clear_flag(boundGrid_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_outline_width(binding.button, 0, 0);
    lv_obj_set_style_opa(binding.button, LV_OPA_COVER, 0);
    if (boundGrid_) lv_obj_update_layout(boundGrid_);
    dragging_ = nullptr;
    dragSourceIndex_ = SIZE_MAX;
    suppressClicksUntil_ = millis() + 500;
}

void HomeReorderController::tileEvent(lv_event_t* e) {
    auto* binding =
        static_cast<Binding*>(lv_event_get_user_data(e));
    if (!binding) return;

    auto& self = HomeReorderController::instance();
    const auto code = lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED&&UiShell::instance().desktopEditing()){self.beginDrag(*binding);return;}

    if (code == LV_EVENT_LONG_PRESSED) {
        self.beginDrag(*binding);
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (self.dragging_ == binding)
            self.updateDrag();
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        if (self.dragging_ == binding)
            self.finishDrag(*binding);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        if (self.dragging_ == binding)
            self.cancelDrag(*binding);
        return;
    }
    if (code == LV_EVENT_CLICKED) {
        if (UiShell::instance().desktopEditing() || self.dragging_ ||
            static_cast<int32_t>(
                millis() - self.suppressClicksUntil_) < 0)
            return;
        self.launch(*binding);
    }
}

void HomeReorderController::loop() {
    DockChange change;
    if(dockQueue_&&xQueueReceive(dockQueue_,&change,0)==pdPASS) {
        if(!SecurityService::instance().tabletLocked()) {
            auto& shortcuts=DockShortcutService::instance();
            const bool ok=change.remove?shortcuts.remove(change.id):shortcuts.add(change.id);
            if(ok)UiShell::instance().requestDockRefresh();
            else EventLog::instance().add("WARN","DOCK","Verknuepfung konnte nicht gespeichert werden");
        }
    }
    // Persist outside the LVGL callback so NVS never blocks touch handling.
    if (pendingMove_.exchange(false, std::memory_order_acq_rel)) {
        const String dragged(pendingDragged_);
        const int8_t pageDirection=pendingPageDirection_;pendingPageDirection_=0;
        String target(pendingTarget_);
        if(pageDirection){
            auto& registry=AppRegistry::instance();registry.rebuild();
            const int page=int(UiShell::instance().desktopPage())+pageDirection;
            size_t visible=0;
            if(page>=0)for(size_t i=0;i<registry.count()&&visible<48;++i){const auto* entry=registry.at(i);
                if(!entry||!entry->launcherVisible||entry->state!=AppRegistryState::Enabled)continue;
                const size_t index=visible++;
                if(index/10==size_t(page)){
                    if(!target.length()||pageDirection>0)target=entry->id;
                }
            }
            pendingAfter_=pageDirection>0;
        }
        if (!SecurityService::instance().tabletLocked()&&HomeAppOrderService::instance().move(
                dragged, target, pendingAfter_)) {
            // Main-loop owned metadata rebuild. HTTP only reads snapshots.
            AppRegistry::instance().rebuild();
            if(pageDirection)UiShell::instance().requestDesktopPage(pageDirection);
            EventLog::instance().add(
                "INFO", "HOME_APP_REORDER",
                dragged + " -> " + target);
        } else {
            EventLog::instance().add(
                "WARN", "HOME_APP_REORDER",
                "Reihenfolge konnte nicht gespeichert werden");
        }
    }

    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastScanMs_) < 200UL)
        return;
    lastScanMs_ = now;

    DisplayUiGuard guard(10);
    if (!guard) return;
    scanAndBind();
}

} // namespace hp
