#pragma once
#include "../../core/AppManager.h"
namespace hp {
    struct NativeCalendarPage;
    class CalendarApp final : public IApp {
      public:
        const char *id() const override { return "calendar"; }
        const char *name() const override { return "Kalender"; }
        const char *icon() const override { return "@calendar"; }
        const char *version() const override { return "1.0.0"; }
        AppKind kind() const override { return AppKind::Native; }
        bool launcherVisible() const override { return true; }
        AppPermissions permissions() const override {
            return perm(AppPermission::CalendarRead) | perm(AppPermission::CalendarWrite);
        }
        bool begin() override { return true; }
        bool hasSettings() const override { return true; }
        const char* webPath() const override { return "/calendar"; }
        void open(lv_obj_t *parent) override;
        void openUsers(lv_obj_t* parent){usersRequested_=true;create(parent,false);}
        bool openSettings(lv_obj_t *parent) override;
        void close() override;
        void setLaunchArguments(const String&) override;

      private:
        NativeCalendarPage *page_ = nullptr;
        bool usersRequested_=false;
        void create(lv_obj_t *parent, bool settings);
    };
} // namespace hp
