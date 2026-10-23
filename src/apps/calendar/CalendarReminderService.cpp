#include "CalendarReminderService.h"
#include "CalendarService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/SecurityService.h"
#include "../../services/TimeService.h"
#include "../../services/IdleService.h"
#include "../../ui/UiTheme.h"
#include "../../ui/UiFonts.h"
#include <memory>
#include <vector>

namespace hp {
    CalendarReminderService &CalendarReminderService::instance() {
        static CalendarReminderService value;
        return value;
    }
    void CalendarReminderService::tick() {
        const auto now = millis();
        if (int32_t(now - next_) < 0 || pending_.exchange(true))
            return;
        if (NetworkWorker::instance().submitLocal(run, this, "cal-reminder"))
            next_ = now + 15000;
        else {
            pending_ = false;
            next_ = now + 1000;
        }
    }
    void CalendarReminderService::run(void *context) {
        auto &self = *static_cast<CalendarReminderService *>(context);
        self.scan();
        self.pending_ = false;
    }
    void CalendarReminderService::scan() {
        auto &service = CalendarService::instance();
        if (!service.storageHealthy())
            return;
        const auto now = TimeService::instance().nowUtc();
        if (now < 1577836800)
            return;
        const uint32_t revision=service.syncRevision();
        const bool masked=SecurityService::instance().tabletLocked() ||
                          !SecurityService::instance().settingsAllowed() ||
                          !SecurityService::instance().enabled(PinKind::Settings);
        // Keep the next due time, not event bodies. Writes/security/clock changes
        // invalidate the cache; an unsuccessful scan never postpones a retry.
        if(cachedRevision_==revision && cachedMasked_==masked &&
           now>=lastScanUtc_ && now<nextScanUtc_)return;
        int64_t nextDue=now+300;
        std::vector<PersonRecord> people;
        std::vector<CalendarRecord> calendars;
        service.copyMetadata(people, calendars);
        const auto began = millis();
        String error;
        Reminder candidates[8]{};
        unsigned count = 0;
        bool overflow=false;
        // No private details leave storage when locked; loopUi also rechecks the lock.
        const bool hide = masked;
        const auto proceed = [&]() {
            if(NetworkWorker::instance().priorityDepth()){error="reminder_yield";return false;}
            NetworkWorker::instance().maintain();
            return uint32_t(millis() - began) < 2000;
        };
        const bool complete = CalendarEventStore::visit(
            [&](const StoredCalendarEvent &event) {
                if (!proceed()) {
                    error = "reminder_budget";
                    return false;
                }
                if (event.event.flags & (EventDeleted | EventExternal) ||
                    event.rules.reminderMinutes < 0)
                    return true;
                bool known = false, privateCalendar = false;
                for (const auto &cal : calendars)
                    if (cal.id == event.event.calendarId) {
                        known = true;
                        privateCalendar = cal.kind == uint8_t(CalendarKind::Personal);
                    }
                if (!known)
                    return true;
                const int64_t offset = int64_t(event.rules.reminderMinutes) * 60;
                const bool expanded = calendar::occurrences(
                    event.event.startUtc, event.event.endUtc, event.rules, now + offset - 120,
                    now + offset + 300,
                    [&](int64_t start, int64_t) {
                        const auto due = start - offset;
                        if(due>now){nextDue=std::min(nextDue,due);return true;}
                        if (due < now - 120)
                            return true;
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (const auto &seen : seen_)
                            if (seen.id == event.event.localId && seen.occurrence == start)
                                return true;
                        for (unsigned i = 0; i < count_; ++i)
                            if (queue_[i].id == event.event.localId &&
                                queue_[i].occurrence == start)
                                return true;
                        for (unsigned i = 0; i < count; ++i)
                            if (candidates[i].id == event.event.localId &&
                                candidates[i].occurrence == start)
                                return true;
                        if (count < 8) {
                            auto &item = candidates[count++];
                            item.id = event.event.localId;
                            item.occurrence = start;
                            strlcpy(item.title,
                                    hide && privateCalendar ? "Privater Termin" : event.event.title,
                                    sizeof(item.title));
                        }else overflow=true;
                        return true;
                    },
                    proceed);
                if (!expanded)
                    error = "reminder_budget";
                return expanded;
            },
            error);
        if (!complete || error.length())
            return; // Failed/partial scans never mark an event delivered.
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_+count>8)overflow=true;
        // If a write overlapped the scan, do not cache an obsolete boundary.
        if(revision==service.syncRevision()){
            cachedRevision_=revision;cachedMasked_=masked;
            lastScanUtc_=now;nextScanUtc_=overflow?now:nextDue;
        }
        for (unsigned i = 0; i < count && count_ < 8; ++i)
            queue_[count_++] = candidates[i];
        if (count_)
            dirty_ = true;
    }
    void CalendarReminderService::dismiss() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_) {
            seen_[nextSeen_++ % 32] = {queue_[0].id, queue_[0].occurrence};
            for (unsigned i = 1; i < count_; ++i)
                queue_[i - 1] = queue_[i];
            --count_;
        }
        if (overlay_) {
            lv_obj_delete(overlay_);
            overlay_ = title_ = nullptr;
        }
        dirty_ = true;
    }
    void CalendarReminderService::loopUi(bool modalBlocked) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return;
        const bool changed = dirty_.exchange(false);
        if (modalBlocked) {
            if (overlay_)
                lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (!count_)
            return;
        if (overlay_ && lv_obj_has_flag(overlay_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            IdleService::instance().activity();
        }
        const bool masked = SecurityService::instance().tabletLocked() ||
                            !SecurityService::instance().settingsAllowed();
        const bool newlyCreated = !overlay_;
        if (!overlay_) {
            overlay_ = lv_obj_create(lv_layer_sys());
            UiTheme::instance().styleScreen(overlay_);
            lv_obj_set_size(overlay_, 480, 190);
            lv_obj_center(overlay_);
            lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
            lv_obj_set_flex_flow(overlay_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(overlay_, 20, 0);
            lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
            title_ = lv_label_create(overlay_);
            lv_obj_set_width(title_, 440);
            lv_label_set_long_mode(title_, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(title_, fontWithGermanFallback(&lv_font_montserrat_24), 0);
            auto *button = lv_button_create(overlay_);
            lv_obj_set_size(button, 180, 48);
            auto *caption = lv_label_create(button);
            lv_label_set_text(caption, "Erledigt");
            lv_obj_center(caption);
            lv_obj_add_event_cb(
                button,
                [](lv_event_t *e) {
                    static_cast<CalendarReminderService *>(lv_event_get_user_data(e))->dismiss();
                },
                LV_EVENT_CLICKED, this);
            IdleService::instance().activity();
        }
        if (changed || newlyCreated || masked_ != masked) {
            masked_ = masked;
            lv_label_set_text_fmt(title_, "Terminerinnerung\n%s",
                                  masked ? "Zum Anzeigen entsperren" : queue_[0].title);
        }
    }
} // namespace hp
