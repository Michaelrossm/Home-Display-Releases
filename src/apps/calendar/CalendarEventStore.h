#pragma once
#include "CalendarTypes.h"
#include "EventDetails.h"
#include "CalendarDate.h"
#include <vector>
#include <functional>
namespace hp {
// Atomic, checksummed event + details + recurrence. Legacy events.bin remains
// readable; a newer record for the same localId supersedes it without deleting it.
struct StoredCalendarEvent {
 uint32_t magic=0x48504345,version=1,checksum=0;
 EventRecord event{};EventDetails details{};calendar::Rules rules{};
 bool valid()const;
 void seal();
};
class CalendarEventStore {
public:
 static constexpr size_t kSdLimit=4096,kInternalLimit=32;
 static bool read(uint64_t id,StoredCalendarEvent&,String& error,bool* legacy=nullptr);
 static bool write(const StoredCalendarEvent&,String& error);
 static bool visit(const std::function<bool(const StoredCalendarEvent&)>&,String& error);
};
}
