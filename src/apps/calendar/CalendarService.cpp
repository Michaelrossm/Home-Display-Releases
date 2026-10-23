#include "CalendarService.h"
#include "CalendarMetadataStore.h"
#include "../../services/StorageService.h"
#include "../../services/TimeService.h"
#include "../../services/PowerManager.h"
#include "../../services/NetworkWorker.h"
#include "../../services/PhonebookService.h"
#include "../../services/SecurityService.h"
#include "../../core/EventBus.h"
#include "../lua/LuaEvents.h"
#include <ctype.h>
#include <esp_random.h>
#include <memory>
#include <new>

namespace hp {

static constexpr const char* kPersonsPath = "/calendar/persons.bin";
static constexpr const char* kCalendarsPath = "/calendar/calendars.bin";
static constexpr const char* kEventsPath = "/calendar/events.bin";
static constexpr const char* kMetaPath = "/calendar/meta.bin";

struct CalendarMeta {
    uint32_t magic = kCalendarStoreMagic;
    uint16_t version = kCalendarStoreVersion;
    uint16_t reserved = 0;
    uint32_t syncRevision = 1;
    uint64_t eventCounter = 1;
};

CalendarService& CalendarService::instance() {
    static CalendarService s;
    return s;
}

uint32_t CalendarService::syncRevision()const{
    // Contact edits invalidate calendar cursors/UI without writing duplicate events.
    return syncRevision_.load()+PhonebookService::instance().revision();
}


int CalendarService::personIndexById(uint32_t id) const {
    for (size_t i = 0; i < personCount_; ++i) {
        if (persons_[i].id == id && (persons_[i].flags&1)) return (int)i;
    }
    return -1;
}

uint16_t CalendarService::participantMaskForPersonId(uint32_t id) const {
    const int index = personIndexById(id);
    return (index >= 0 && index < 16) ? (uint16_t)(1u << index) : 0;
}

bool CalendarService::begin() {
    std::lock_guard<std::recursive_mutex> profileLock(profileMutex());
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& storage = StorageService::instance();

    CalendarMeta meta;
    if (storage.readBinary(StorageClass::UserData, kMetaPath, &meta, sizeof(meta)) == sizeof(meta) &&
        meta.magic == kCalendarStoreMagic &&
        meta.version == kCalendarStoreVersion) {
        syncRevision_ = max<uint32_t>(1, meta.syncRevision);
        eventCounter_ = max<uint64_t>(1, meta.eventCounter);
    }

    if(!recoverProfiles())return false;
    const bool personsOk=loadPersons(),calendarsOk=loadCalendars();
    storageHealthy_=personsOk&&calendarsOk;
    if(!storageHealthy_)return false;

    // Always provide shared family and virtual birthdays calendar.
    upsertCalendar("Familie", CalendarKind::Family, 0);
    upsertCalendar("Geburtstage", CalendarKind::BirthdaysVirtual, 0);
    upsertCalendar("Feiertage", CalendarKind::HolidaysVirtual, 0);
    upsertCalendar("Haushalt", CalendarKind::Household, 0);

    // Do not scan a potentially large event archive on the boot/UI task.
    // The existing scheduler queues the first cache rebuild on NetworkWorker.
    lastRefresh_=millis()-300000UL;
    return true;
}

bool CalendarService::loadPersons() {
    personCount_ = 0;
    bool present=false;if(!calendar::loadMetadata("/calendar/persons.v2.bin",persons_,personCount_,present))return false;if(present)return true;

    auto& storage = StorageService::instance();
    fs::FS* fs = storage.fsFor(StorageClass::UserData);
    if (!fs) return false;

    File f = fs->open(storage.pathFor(StorageClass::UserData, kPersonsPath), FILE_READ);
    if (!f) return true;
    if(f.size()%sizeof(PersonRecord)||f.size()>sizeof(persons_))return false;

    while (personCount_ < HOMEPANEL_CALENDAR_MAX_PERSONS &&
           f.available() >= (int)sizeof(PersonRecord)) {
        PersonRecord r{};
        if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) return false;
        if(!r.id||!r.email[0]||!memchr(r.email,0,sizeof(r.email))||!memchr(r.displayName,0,sizeof(r.displayName))||r.colorIndex>15)return false;
        for(size_t i=0;i<personCount_;++i)if(persons_[i].id==r.id)return false;
        persons_[personCount_++] = r;
    }
    f.close();
    return true;
}

bool CalendarService::loadCalendars() {
    calendarCount_ = 0;
    bool present=false;if(!calendar::loadMetadata("/calendar/calendars.v2.bin",calendars_,calendarCount_,present))return false;if(present)return true;

    auto& storage = StorageService::instance();
    fs::FS* fs = storage.fsFor(StorageClass::UserData);
    if (!fs) return false;

    File f = fs->open(storage.pathFor(StorageClass::UserData, kCalendarsPath), FILE_READ);
    if (!f) return true;
    if(f.size()%sizeof(CalendarRecord)||f.size()>sizeof(calendars_))return false;

    while (calendarCount_ < HOMEPANEL_CALENDAR_MAX_CALENDARS &&
           f.available() >= (int)sizeof(CalendarRecord)) {
        CalendarRecord r{};
        if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) return false;
        if(!r.id||!r.name[0]||!memchr(r.name,0,sizeof(r.name))||!memchr(r.remoteAccountId,0,sizeof(r.remoteAccountId))||r.kind>5||r.enabled>1||r.colorIndex>15)return false;
        for(size_t i=0;i<calendarCount_;++i)if(calendars_[i].id==r.id)return false;
        calendars_[calendarCount_++] = r;
    }
    f.close();
    return true;
}

bool CalendarService::savePersons() {
    return calendar::saveMetadata("/calendar/persons.v2.bin",persons_,personCount_);
}

bool CalendarService::saveCalendars() {
    return calendar::saveMetadata("/calendar/calendars.v2.bin",calendars_,calendarCount_);
}

void CalendarService::bumpRevision() {
    cacheInvalidated_=true;
    ++syncRevision_;
    if (!syncRevision_) syncRevision_ = 1;

    CalendarMeta meta;
    meta.syncRevision = syncRevision_;
    meta.eventCounter = eventCounter_;

    StorageService::instance().writeBinaryAtomic(
        StorageClass::UserData,
        kMetaPath,
        &meta,
        sizeof(meta)
    );

    EventBus::instance().publish({EventType::CalendarChanged, nullptr});
    publishLuaEvent(nullptr,"calendar.updated");
}


bool CalendarService::upsertCalendar(
    const char* name,
    CalendarKind kind,
    uint32_t ownerPersonId,
    CalendarProvider provider
) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!storageHealthy_||!name || !name[0]||strlen(name)>=sizeof(CalendarRecord::name)||uint8_t(kind)>5||(ownerPersonId&&personIndexById(ownerPersonId)<0)) return false;

    uint32_t id = 2166136261u;
    const uint8_t* p = (const uint8_t*)name;
    while (*p) { id ^= *p++; id *= 16777619u; }
    id ^= ownerPersonId;
    id *= 16777619u;
    id ^= (uint8_t)kind;
    if (!id) id = 1;

    for (size_t i = 0; i < calendarCount_; ++i) {
        if (calendars_[i].id == id) return calendars_[i].ownerPersonId==ownerPersonId&&calendars_[i].kind==uint8_t(kind)&&calendars_[i].provider==uint8_t(provider)&&!strcmp(calendars_[i].name,name);
    }

    if (calendarCount_ >= HOMEPANEL_CALENDAR_MAX_CALENDARS) return false;

    auto& c = calendars_[calendarCount_++];
    c.id = id;
    c.ownerPersonId = ownerPersonId;
    c.kind = static_cast<uint8_t>(kind);
    c.provider = static_cast<uint8_t>(provider);
    c.enabled = 1;
    strlcpy(c.name, name, sizeof(c.name));

    if (!saveCalendars()) {
        --calendarCount_;
        return false;
    }
    bumpRevision();
    return true;
}

uint64_t CalendarService::nextLocalEventId() {
    uint64_t id = ((uint64_t)syncRevision_ << 32) ^ eventCounter_++ ^ esp_random();
    if (!id) id = eventCounter_++;
    return id;
}

namespace {
String detailsPath(uint64_t id) {
    char path[64];snprintf(path,sizeof(path),"/calendar/details/%08lx%08lx.bin",
        (unsigned long)(id>>32),(unsigned long)id);return String(path);
}
}
bool CalendarService::saveEventDetails(const EventDetails& details) {
    if(!details.valid())return false;
    std::unique_ptr<StoredEventDetails> record(new(std::nothrow) StoredEventDetails{});if(!record)return false;
    record->details=details;record->checksum=detailChecksum(&record->details,sizeof(record->details));
    return StorageService::instance().writeAtomic(StorageArea::UserData,detailsPath(details.localId),record.get(),sizeof(*record));
}
bool CalendarService::loadEventDetails(uint64_t id, EventDetails& output) {
    output={};if(!id)return false;std::unique_ptr<StoredEventDetails> record(new(std::nothrow) StoredEventDetails{});if(!record)return false;
    auto validator=[&](fs::FS& fs,const String& path){auto file=fs.open(path,"r");return file&&file.size()==sizeof(*record)&&
        file.read(reinterpret_cast<uint8_t*>(record.get()),sizeof(*record))==sizeof(*record)&&record->valid(id);};
    auto& storage=StorageService::instance();if(!storage.recoverAtomicWrite(StorageArea::UserData,detailsPath(id),validator))return false;
    size_t actual=0;if(!storage.read(StorageArea::UserData,detailsPath(id),record.get(),sizeof(*record),&actual)||actual!=sizeof(*record)||!record->valid(id))return false;
    output=record->details;return true;
}
bool CalendarService::appendEvent(EventRecord event,const EventDetails& input){
    if(input.localId&&input.localId!=event.localId)return false;
    std::unique_ptr<StoredCalendarEvent> value(new(std::nothrow) StoredCalendarEvent);if(!value)return false;
    value->event=event;value->details=input;String error;
    return saveEvent(*value,event.localId?event.revision:0,error);
}
bool CalendarService::appendEvent(EventRecord event) {
    std::unique_ptr<EventDetails> details(new(std::nothrow) EventDetails);if(!details)return false;
    details->localId=event.localId;detailPreview(details->location,sizeof(details->location),event.location);detailPreview(details->notes,sizeof(details->notes),event.notes);
    return appendEvent(event,*details);
}

bool CalendarService::addLocalEvent(const char* title, int64_t startUtc, int64_t endUtc) {
    if (!title || !title[0] || startUtc <= 0 || endUtc <= startUtc) return false;

    uint32_t familyCalendar = 0;
    for (size_t i = 0; i < calendarCount_; ++i) {
        const auto& calendar = calendars_[i];
        if (calendar.enabled && calendar.kind == static_cast<uint8_t>(CalendarKind::Family) &&
            calendar.provider == static_cast<uint8_t>(CalendarProvider::LocalHomePanel)) {
            familyCalendar = calendar.id;
            break;
        }
    }
    if (!familyCalendar) return false;

    EventRecord event{};
    event.calendarId = familyCalendar;
    event.startUtc = startUtc;
    event.endUtc = endUtc;
    strlcpy(event.title, title, sizeof(event.title));
    return appendEvent(event);
}

bool CalendarService::rebuildUpcoming(int64_t fromUtc, int64_t horizonSeconds) {
    std::lock_guard<std::recursive_mutex> profileLock(profileMutex());
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(!fromUtc)fromUtc=TimeService::instance().nowUtc();
    Query request;request.fromUtc=fromUtc;request.toUtc=fromUtc+horizonSeconds;
    std::vector<Occurrence> result;bool more=false;String error;
    if(!query(request,result,more,error))return false;
    // Only replace a valid cache; read errors do not erase existing appointments.
    upcomingCount_=0;
    for(const auto& row:result){
        const auto& e=row.event;auto& u=upcoming_[upcomingCount_++];u={};
        u.localId=e.localId;u.calendarId=e.calendarId;u.participantMask=e.participantMask;
        u.flags=e.flags;u.startUtc=e.startUtc;u.endUtc=e.endUtc;
        strlcpy(u.title,e.title,sizeof(u.title));
    }
    return true;
}

void CalendarService::backgroundTick() {
    if (!cacheInvalidated_ && upcomingBookRevision_==PhonebookService::instance().revision() && millis() - lastRefresh_ < 300000UL) return;
    if(cachePending_.exchange(true))return;
    if(NetworkWorker::instance().submit([](void* context){
        auto& self=*static_cast<CalendarService*>(context);PerformanceGuard boost;
        self.cacheInvalidated_=false;
        self.upcomingBookRevision_=PhonebookService::instance().revision();
        self.rebuildUpcoming(); // Keep the last valid cache on failure; retry on the normal cadence.
        self.cachePending_=false;
    },this,"calendar-cache"))lastRefresh_=millis();
    else cachePending_=false;
}

}
