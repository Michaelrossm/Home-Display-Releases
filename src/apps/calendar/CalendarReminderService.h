#pragma once
#include <Arduino.h>
#include <atomic>
#include <mutex>
#include <lvgl.h>
namespace hp {
    // Local reminders use the existing short-job worker, not the upload lane or an app task.
    class CalendarReminderService {
      public:
        static CalendarReminderService &instance();
        void tick();
        void loopUi(bool modalBlocked = false); // Caller owns DisplayUiGuard.
        bool urgent() const { return dirty_.load(); }

      private:
        struct Reminder {
            uint64_t id = 0;
            int64_t occurrence = 0;
            char title[96]{};
        };
        static void run(void *);
        void scan();
        void dismiss();
        std::mutex mutex_;
        struct Key {
            uint64_t id = 0;
            int64_t occurrence = 0;
        };
        Reminder queue_[8]{};
        Key seen_[32]{};
        unsigned count_ = 0, nextSeen_ = 0;
        uint32_t next_ = 0;
        uint32_t cachedRevision_ = 0;
        int64_t lastScanUtc_ = 0, nextScanUtc_ = 0;
        bool cachedMasked_ = false;
        std::atomic<bool> pending_{false}, dirty_{false};
        lv_obj_t *overlay_ = nullptr;
        lv_obj_t *title_ = nullptr;
        bool masked_ = false;
    };
} // namespace hp
