#pragma once
#include "CalendarService.h"
#include "CalendarViewModel.h"
#include <ArduinoJson.h>
#include <array>

namespace hp {
    class CalendarController {
      public:
        static constexpr size_t maxVisibleEvents = 256;
        struct Item {
            EventRecord event{};
            int64_t occurrence = 0;
        };
        calendar::Date selected{};
        calendar::ViewMode mode = calendar::ViewMode::Month;
        uint8_t preset = 0;
        uint32_t person = 0, calendarId = 0;
        std::vector<PersonRecord> people;
        uint16_t participantMasks[16]{};
        std::vector<CalendarRecord> calendars,features;
        std::vector<Item> events;
        JsonDocument details, status, region, personLinks, personNames, contacts, postal;
        JsonDocument profileDetails;
        bool postalReady=false;
        bool dateGerman=false;
        uint32_t statusGeneration = 0;
        uint32_t savedPersonId=0;
        uint32_t peopleGeneration=0;
        String message;
        bool busy = false, truncated = false, dirty = true, detailsReady = false,
             actionDone = false;
        void open(bool usersOnly=false);
        void close();
        void poll();
        void select(calendar::Date, calendar::ViewMode);
        void refresh(bool invalidate = true);
        bool action(const char *operation, JsonDocument &arguments);
        bool loadDetails(const char *id);
        uint32_t color(uint32_t calendarId) const;
        bool editable(uint32_t calendarId) const;
        uint32_t firstEditable() const;
        const char *owner() const { return owner_; }
        void setOwner(const char* value){owner_=value;}

      private:
        struct Cache {
            int64_t from = 0, to = 0;
            uint32_t person = 0, calendarId = 0, revision = 0, cloud = 0;
            uint8_t preset = 0;
            bool complete = false;
            std::vector<Item> events;
        };
        std::array<Cache, 3> cache_{};
        unsigned nextCache_ = 0, bootstrap_ = 0;
        uint32_t job_ = 0, retryAt_ = 0, revision_ = 0, cloudRevision_ = 0;
        String operation_, cursorId_;
        int64_t cursorStart_ = 0;
        bool queryPending_ = false, active_ = false, usersOnly_=false;
        bool regionRefreshPending_ = true; // Region is stable across normal event revisions.
        const char* owner_="@calendar-native";
        bool submit(const char *, JsonDocument &);
        void query();
        void consume(JsonDocument &);
    };
} // namespace hp
