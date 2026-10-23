#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "HomePanelConfig.h"

namespace hp {

static constexpr uint32_t kCalendarStoreMagic = 0x48504335; // "HPC5"
static constexpr uint16_t kCalendarStoreVersion = 1;

enum class CalendarKind : uint8_t {
    Personal = 0,
    Family = 1,
    Household = 2,
    BirthdaysVirtual = 3,
    External = 4,
    HolidaysVirtual = 5
};

enum EventFlags : uint16_t {
    EventAllDay = 1u << 0,
    EventRecurring = 1u << 1,
    EventReadOnly = 1u << 2,
    EventExternal = 1u << 3,
    EventDeleted = 1u << 4
};

enum class CalendarProvider : uint8_t {
    LocalHomePanel = 0,
    GenericCalDav = 1,
    GoogleCalDav = 2,
    ICloud = 3
};

#pragma pack(push, 1)

struct StoreHeader {
    uint32_t magic = kCalendarStoreMagic;
    uint16_t version = kCalendarStoreVersion;
    uint16_t recordSize = 0;
    uint32_t recordCount = 0;
    uint32_t syncRevision = 1;
    uint32_t crc32 = 0;
};

struct PersonRecord {
    uint32_t id = 0;
    uint32_t revision = 1;
    uint16_t flags = 1;
    uint8_t colorIndex = 0;
    uint8_t birthdayMonth = 0;
    uint8_t birthdayDay = 0;
    uint16_t birthdayYear = 0;
    char email[64]{};
    char displayName[40]{};
};

struct CalendarRecord {
    uint32_t id = 0;
    uint32_t ownerPersonId = 0; // 0 = shared/family
    uint32_t revision = 1;
    uint8_t kind = static_cast<uint8_t>(CalendarKind::Personal);
    uint8_t colorIndex = 0;
    uint8_t enabled = 1;
    uint8_t provider = static_cast<uint8_t>(CalendarProvider::LocalHomePanel);
    char name[40]{};
    char remoteAccountId[48]{};
};

struct EventRecord {
    uint64_t localId = 0;
    uint32_t calendarId = 0;
    uint32_t ownerPersonId = 0;
    uint32_t revision = 1;
    uint16_t participantMask = 0;
    uint16_t flags = 0;
    int64_t startUtc = 0;
    int64_t endUtc = 0;
    char uid[56]{};
    char title[72]{};
    char location[48]{};
    char notes[96]{};
    char externalHref[96]{};
};

struct SyncAccountRecord {
    uint32_t id = 0;
    uint32_t personId = 0;
    uint32_t revision = 1;
    uint8_t provider = static_cast<uint8_t>(CalendarProvider::GenericCalDav);
    uint8_t enabled = 0;
    uint16_t flags = 0;
    char accountEmail[64]{};
    char server[96]{};
    char remotePrincipal[96]{};
    char syncToken[96]{};
};

#pragma pack(pop)

struct UpcomingEvent {
    uint64_t localId = 0;
    uint32_t calendarId = 0;
    uint16_t participantMask = 0;
    uint16_t flags = 0;
    int64_t startUtc = 0;
    int64_t endUtc = 0;
    char title[72]{};
};

}
