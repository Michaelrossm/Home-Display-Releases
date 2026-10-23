#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace hp {
inline constexpr size_t kCloudCalendarEvents=128;
inline void detailPreview(char* target,size_t capacity,const char* source){
    if(!target||!capacity)return;if(!source){target[0]=0;return;}
    size_t n=strlen(source);if(n>=capacity)n=capacity-1;
    while(n&&(uint8_t(source[n])&0xc0)==0x80)--n;
    memcpy(target,source,n);target[n]=0;
}
// Separate from EventRecord: existing version-1 event files remain compatible.
struct EventDetails {
    uint64_t localId = 0;
    char location[513]{}, street[193]{}, houseNumber[33]{}, postalCode[33]{};
    char city[129]{}, region[129]{}, country[97]{}, phone[97]{};
    char notes[4097]{};
    uint8_t notesHtml = 0; // Never render provider HTML as executable markup.
    uint8_t truncated = 0;
    bool valid() const {
        return localId && notesHtml<=1 && truncated<=1 && memchr(location,0,sizeof(location)) && memchr(street,0,sizeof(street)) &&
            memchr(houseNumber,0,sizeof(houseNumber)) && memchr(postalCode,0,sizeof(postalCode)) &&
            memchr(city,0,sizeof(city)) && memchr(region,0,sizeof(region)) &&
            memchr(country,0,sizeof(country)) && memchr(phone,0,sizeof(phone)) && memchr(notes,0,sizeof(notes));
    }
};
inline uint32_t detailChecksum(const void* data,size_t size) {
    uint32_t hash=2166136261u;const auto* bytes=static_cast<const uint8_t*>(data);
    while(size--){hash^=*bytes++;hash*=16777619u;}return hash;
}
struct StoredEventDetails {
    uint32_t magic=0x48504432,version=1,checksum=0;
    EventDetails details{};
    bool valid(uint64_t id)const{return magic==0x48504432&&version==1&&details.localId==id&&details.valid()&&checksum==detailChecksum(&details,sizeof(details));}
};
struct EventDetailsSnapshot {
    uint32_t magic=0x48504431,version=1,count=0,eventsChecksum=0,detailsChecksum=0;
    EventDetails items[kCloudCalendarEvents]{};
    static constexpr size_t legacyBytes(){return offsetof(EventDetailsSnapshot,items)+32*sizeof(EventDetails);}
    static constexpr size_t storedBytes(size_t count){return offsetof(EventDetailsSnapshot,items)+count*sizeof(EventDetails);}
    static bool compatibleSize(size_t bytes){return bytes>=storedBytes(0)&&bytes<=sizeof(EventDetailsSnapshot)&&(bytes-storedBytes(0))%sizeof(EventDetails)==0;}
    bool valid(const void* events,size_t size)const {
        if(magic!=0x48504431||version!=1||count>kCloudCalendarEvents||eventsChecksum!=detailChecksum(events,size)||
           detailsChecksum!=detailChecksum(items,count*sizeof(EventDetails)))return false;
        for(uint32_t i=0;i<count;++i)if(!items[i].valid())return false;return true;
    }
};
}
