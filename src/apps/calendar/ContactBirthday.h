#pragma once
#include "CalendarDate.h"
#include "EventDetails.h"
#include <cstdint>
#include <cstdio>
namespace hp::calendar {
template<class Contact,class Event> bool contactBirthday(const Contact& c,int year,uint32_t calendarId,Event& e){
 const auto b=c.birthday;
 if(!b.month||year<b.year)return false;
 // In non-leap years a February 29 birthday is shown on February 28.
 const unsigned day=b.month==2&&b.day==29&&!leap(year)?28:b.day;
 if(!validDate(year,b.month,day))return false;
 uint64_t hash=14695981039346656037ULL;
 for(const unsigned char* p=reinterpret_cast<const unsigned char*>(c.id.c_str());*p;++p){hash^=*p;hash*=1099511628211ULL;}
 hash^=uint16_t(year);hash*=1099511628211ULL;
 e={};e.calendarId=calendarId;e.localId=0xC000000000000000ULL|(hash&0x0FFFFFFFFFFFFFFFULL);
 e.startUtc=localEpoch({year,b.month,int(day)});e.endUtc=localEpoch(dateFromDay(dayNumber(year,b.month,day)+1));
 char title[320];if(b.year)snprintf(title,sizeof(title),"%s · %u. Geburtstag",c.name.c_str(),unsigned(year-b.year));
 else snprintf(title,sizeof(title),"Geburtstag: %s",c.name.c_str());
 detailPreview(e.title,sizeof(e.title),title);
 return e.startUtc&&e.endUtc>e.startUtc;
}
}
