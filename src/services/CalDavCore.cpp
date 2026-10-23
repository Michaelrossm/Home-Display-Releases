#include "CalDavCore.h"
#include <time.h>
#include <memory>
#include <new>

namespace hp {

CalDavCore& CalDavCore::instance() {
    static CalDavCore c;
    return c;
}

String CalDavCore::principalPath(const PersonRecord& p) const {
    char b[48];
    snprintf(b, sizeof(b), "/dav/principals/%08lx/", (unsigned long)p.id);
    return String(b);
}

String CalDavCore::calendarHomePath(const PersonRecord& p) const {
    char b[48];
    snprintf(b, sizeof(b), "/dav/calendars/%08lx/", (unsigned long)p.id);
    return String(b);
}

String CalDavCore::calendarPath(
    const PersonRecord& p,
    const CalendarRecord& c
) const {
    char b[72];
    snprintf(
        b,
        sizeof(b),
        "/dav/calendars/%08lx/%08lx/",
        (unsigned long)p.id,
        (unsigned long)c.id
    );
    return String(b);
}

String CalDavCore::eventHref(const EventRecord& e) const {
    char b[88];
    snprintf(
        b,
        sizeof(b),
        "/dav/events/%08lx%08lx.ics",
        (unsigned long)(e.localId >> 32),
        (unsigned long)e.localId
    );
    return String(b);
}

String CalDavCore::etag(const EventRecord& e) const {
    char b[40];
    snprintf(
        b,
        sizeof(b),
        "\"hp-%08lx-%08lx\"",
        (unsigned long)e.revision,
        (unsigned long)(e.localId ^ (e.localId >> 32))
    );
    return String(b);
}

String CalDavCore::syncToken(uint32_t revision) const {
    char b[40];
    snprintf(b, sizeof(b), "http://homepanel.local/ns/sync/%08lx", (unsigned long)revision);
    return String(b);
}

String CalDavCore::escapeText(const char* value) {
    String out;
    if (!value) return out;

    for (const char* p = value; *p; ++p) {
        switch (*p) {
            case '\\': out += "\\\\"; break;
            case ';': out += "\\;"; break;
            case ',': out += "\\,"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default: out += *p; break;
        }
    }
    return out;
}

String CalDavCore::utcStamp(int64_t unixTime) {
    time_t t = (time_t)unixTime;
    struct tm tm{};
    gmtime_r(&t, &tm);
    char b[20];
    strftime(b, sizeof(b), "%Y%m%dT%H%M%SZ", &tm);
    return String(b);
}

String CalDavCore::serializeEvent(const EventRecord& e) const {
    std::unique_ptr<EventDetails> details(new(std::nothrow) EventDetails{});if(!details)return String();details->localId=e.localId;
    strlcpy(details->location,e.location,sizeof(details->location));strlcpy(details->notes,e.notes,sizeof(details->notes));
    return serializeEvent(e,*details);
}
String CalDavCore::serializeEvent(const EventRecord& e, const EventDetails& details) const {
    if(!details.valid()||details.localId!=e.localId)return String();
    // Kept compact deliberately; CalDAV transport can stream this later.
    String out;
    out.reserve(384);

    out += "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//HomePanel//Calendar 1.0//DE\r\n";
    out += "CALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n";
    out += "UID:";
    out += e.uid;
    out += "\r\n";
    out += "DTSTAMP:";
    out += utcStamp(time(nullptr));
    out += "\r\n";

    if (e.flags & EventAllDay) {
        time_t t = (time_t)e.startUtc;
        struct tm tm{};
        gmtime_r(&t, &tm);
        char b[12];
        strftime(b, sizeof(b), "%Y%m%d", &tm);
        out += "DTSTART;VALUE=DATE:";
        out += b;
        out += "\r\n";
    } else {
        out += "DTSTART:";
        out += utcStamp(e.startUtc);
        out += "\r\nDTEND:";
        out += utcStamp(e.endUtc);
        out += "\r\n";
    }

    out += "SUMMARY:";
    out += escapeText(e.title);
    out += "\r\n";

    String address=details.location;
    if(!address.length()){
        address=details.street;
        if(details.houseNumber[0]){if(address.length())address+=' ';address+=details.houseNumber;}
        if(details.postalCode[0]||details.city[0]){if(address.length())address+=", ";address+=details.postalCode;if(details.postalCode[0]&&details.city[0])address+=' ';address+=details.city;}
        if(details.country[0]){if(address.length())address+=", ";address+=details.country;}
    }
    if (address.length()) {
        out += "LOCATION:";
        out += escapeText(address.c_str());
        out += "\r\n";
    }
    if (details.notes[0]) {
        out += "DESCRIPTION:";
        out += escapeText(details.notes);
        out += "\r\n";
    }

    if(details.phone[0]){out+="CONTACT:";out+=escapeText(details.phone);out+="\r\n";}
    auto property=[&](const char* name,const char* value){if(value[0]){out+=name;out+=':';out+=escapeText(value);out+="\r\n";}};
    property("X-HOMEPANEL-STREET",details.street);property("X-HOMEPANEL-HOUSE-NUMBER",details.houseNumber);
    property("X-HOMEPANEL-POSTAL-CODE",details.postalCode);property("X-HOMEPANEL-CITY",details.city);
    property("X-HOMEPANEL-REGION",details.region);property("X-HOMEPANEL-COUNTRY",details.country);
    out += "END:VEVENT\r\nEND:VCALENDAR\r\n";
    // RFC5545: fold at 75 octets, never in the middle of a UTF-8 codepoint.
    String folded;folded.reserve(out.length()+out.length()/70*3);size_t column=0;
    for(size_t i=0;i<out.length();){
        if(out[i]=='\r'&&i+1<out.length()&&out[i+1]=='\n'){folded+="\r\n";column=0;i+=2;continue;}
        size_t bytes=1;const uint8_t c=uint8_t(out[i]);if((c&0xe0)==0xc0)bytes=2;else if((c&0xf0)==0xe0)bytes=3;else if((c&0xf8)==0xf0)bytes=4;
        if(i+bytes>out.length())bytes=1;
        if(column+bytes>75){folded+="\r\n ";column=1;}
        for(size_t j=0;j<bytes;++j)folded+=out[i++];column+=bytes;
    }
    return folded;
}

}
