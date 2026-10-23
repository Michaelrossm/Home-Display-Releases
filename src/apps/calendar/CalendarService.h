#pragma once
#include "CalendarTypes.h"
#include "EventDetails.h"
#include "CalendarEventStore.h"
#include "CalendarHolidays.h"
#include <mutex>
#include <atomic>
#include <ArduinoJson.h>
#include "../../core/BackgroundScheduler.h"

namespace hp {
struct LocalPhonebook;

class CalendarService : public IBackgroundJob {
public:
    static CalendarService& instance();

    bool begin();
    static std::recursive_mutex& profileMutex(){static std::recursive_mutex value;return value;}
    bool saveProfile(JsonVariantConst,JsonDocument&,String&);
    bool removeProfile(uint32_t,bool,String&);
    bool recoverProfiles();
    bool migratePhonebookUsers(LocalPhonebook&,String&);
    struct Occurrence {EventRecord event{};int64_t originalStartUtc=0;};
    struct Query {int64_t fromUtc=0,toUtc=0,afterStartUtc=0;uint64_t afterId=0;uint16_t offset=0,limit=32;uint32_t calendarId=0,personId=0;uint32_t selectedCalendars[4]{};uint8_t selectedCalendarCount=0,preset=0;const std::atomic<bool>* cancelled=nullptr;};
    bool query(const Query&,std::vector<Occurrence>&,bool& more,String& error);
    bool eventDetails(uint64_t,StoredCalendarEvent&,String& error);
    bool saveEvent(StoredCalendarEvent&,uint32_t expectedRevision,String& error);
    bool deleteEvent(uint64_t,uint32_t expectedRevision,int64_t occurrenceUtc,String& error);
    bool setVisibility(uint32_t,bool,String& error);
    bool setCalendarAppearance(uint32_t,const char*,uint8_t,String& error);
    bool setRegion(const calendar::Region&,String& error);
    calendar::Region region()const;
    void copyMetadata(std::vector<PersonRecord>&,std::vector<CalendarRecord>&)const;
    void refreshExternalCalendars(); // Worker only; cache discovery is central.
    bool linkAccountPerson(uint8_t slot,uint32_t personId,String& error);
    bool storageHealthy()const{return storageHealthy_;}
    bool canEditCalendar(uint32_t)const;

    size_t personCount() const { size_t count=0;for(size_t i=0;i<personCount_;++i)count+=bool(persons_[i].flags&1);return count; }
    const PersonRecord* personAt(size_t i) const {
        for(size_t index=0;index<personCount_;++index)if(persons_[index].flags&1){if(!i)return &persons_[index];--i;}return nullptr;
    }

    size_t calendarCount() const { return calendarCount_; }
    const CalendarRecord* calendarAt(size_t i) const {
        return i < calendarCount_ ? &calendars_[i] : nullptr;
    }

    size_t upcomingCount() const { return upcomingCount_; }
    const UpcomingEvent* upcomingAt(size_t i) const {
        return i < upcomingCount_ ? &upcoming_[i] : nullptr;
    }

    bool upsertCalendar(
        const char* name,
        CalendarKind kind,
        uint32_t ownerPersonId = 0,
        CalendarProvider provider = CalendarProvider::LocalHomePanel
    );

    bool appendEvent(EventRecord event);
    bool appendEvent(EventRecord event,const EventDetails& details);
    // Storage operations: call from a worker, never from an LVGL callback.
    bool saveEventDetails(const EventDetails& details);
    bool loadEventDetails(uint64_t localId, EventDetails& output);
    bool addLocalEvent(const char* title, int64_t startUtc, int64_t endUtc);
    bool rebuildUpcoming(int64_t fromUtc = 0, int64_t horizonSeconds = 90LL * 86400LL);

    int personIndexById(uint32_t id) const;
    uint16_t participantMaskForPersonId(uint32_t id) const;

    uint32_t syncRevision() const;
    uint32_t metadataRevision()const{return syncRevision_.load();}
    void backgroundTick() override;
    uint32_t backgroundIntervalMs() const override { return 300000UL; }

private:
    mutable std::recursive_mutex mutex_;
    bool storageHealthy_=false;
    std::atomic<bool> cachePending_{false};
    bool loadPersons();
    bool loadCalendars();
    bool savePersons();
    bool saveCalendars();
    bool commitProfileSnapshot(const PersonRecord*,size_t,const CalendarRecord*,size_t,const String&,const String&,String&);

    void bumpRevision();
    uint64_t nextLocalEventId();

    PersonRecord persons_[HOMEPANEL_CALENDAR_MAX_PERSONS]{};
    CalendarRecord calendars_[HOMEPANEL_CALENDAR_MAX_CALENDARS]{};
    UpcomingEvent upcoming_[HOMEPANEL_CALENDAR_UPCOMING_CACHE]{};

    size_t personCount_ = 0;
    size_t calendarCount_ = 0;
    size_t upcomingCount_ = 0;
    std::atomic<uint32_t> syncRevision_{1};
    std::atomic<bool> cacheInvalidated_{false};
    std::atomic<uint32_t> upcomingBookRevision_{0};
    uint64_t eventCounter_ = 1;
    uint32_t lastRefresh_ = 0;
};

}
