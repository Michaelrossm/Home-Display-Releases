#pragma once
#include <Arduino.h>
#include "../apps/calendar/CalendarTypes.h"
#include "CalDavDiscovery.h"
#include "../apps/calendar/EventDetails.h"

namespace hp {

class CalDavCore {
public:
    static CalDavCore& instance();
    bool parseDiscovery(const char* xml,size_t length,DavDiscovery& out)const{return CalDavDiscovery::parse(xml,length,out);}

    String principalPath(const PersonRecord& person) const;
    String calendarHomePath(const PersonRecord& person) const;
    String calendarPath(const PersonRecord& person, const CalendarRecord& calendar) const;
    String eventHref(const EventRecord& event) const;

    String etag(const EventRecord& event) const;
    String syncToken(uint32_t revision) const;

    // Generates a compact RFC5545 VEVENT/VCALENDAR representation.
    // Transport (PROPFIND/REPORT/PUT/DELETE) will use the same CalendarService store.
    String serializeEvent(const EventRecord& event) const;
    String serializeEvent(const EventRecord& event, const EventDetails& details) const;

private:
    static String escapeText(const char* value);
    static String utcStamp(int64_t unixTime);
};

}
