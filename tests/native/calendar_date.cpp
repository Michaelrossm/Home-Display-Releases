#include <cassert>
#include <cstdlib>
#include <vector>
#include <string>
#include "src/apps/calendar/CalendarDate.h"
#include "src/apps/calendar/CalendarHolidays.h"
using namespace hp::calendar;
int main(){
#ifdef _WIN32
 _putenv_s("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3");_tzset();
#else
 setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();
#endif
 assert(validDate(2000,2,29)&&!validDate(1900,2,29)&&!validDate(2025,2,29)&&!validDate(2026,4,31));
 for(int y=1900;y<2200;++y)for(int m=1;m<=12;++m)for(int d=1;d<=daysInMonth(y,m);++d){auto a=dateFromDay(dayNumber(y,m,d));assert(a.year==y&&a.month==m&&a.day==d);assert(weekday(y,m,d)>=0&&weekday(y,m,d)<7);}
 assert(weekday(2026,10,9)==4);auto e=easter(2026);assert(e.month==4&&e.day==5);
 Region r;std::vector<std::string> names;auto count=[&](int year){names.clear();holidays(year,r,[&](Date,const char* s){names.emplace_back(s);});return names.size();};
 assert(count(2026)==12);r.assumption=true;assert(count(2026)==13);r.augsburg=true;assert(count(2026)==14);
 for(const char* state:{"BW","BY","BE","BB","HB","HH","HE","MV","NI","NW","RP","SL","SN","ST","SH","TH"})assert(validRegion(state));assert(!validRegion("DE"));assert(!validRegion(nullptr));
 strcpy(r.state,"BE");r.assumption=r.augsburg=false;assert(count(2026)==10&&count(2025)==11&&count(2018)==9);
 strcpy(r.state,"SN");assert(count(2026)==11);r.corpusChristiMunicipality=true;assert(count(2026)==12);
 bool repentance=false;holidays(2026,r,[&](Date d,const char* title){if(!strcmp(title,"Buß- und Bettag")){repentance=true;assert(d.month==11&&d.day==18&&weekday(d.year,d.month,d.day)==2);}});assert(repentance);
 Rules rules;rules.repeat=Repeat::Monthly;rules.count=3;std::vector<int> days;
 const auto start=localEpoch({2026,1,31},10),end=localEpoch({2026,1,31},11);
 assert(occurrences(start,end,rules,localEpoch({2026,1,1}),localEpoch({2026,7,1}),[&](int64_t s,int64_t){auto d=localDate(s);days.push_back(d.month);assert(d.day==31);return true;}));assert((days==std::vector<int>{1,3,5}));
 rules={};rules.repeat=Repeat::Yearly;days.clear();assert(occurrences(localEpoch({2024,2,29},9),localEpoch({2024,2,29},10),rules,localEpoch({2024,1,1}),localEpoch({2030,1,1}),[&](int64_t s,int64_t){days.push_back(localDate(s).year);return true;}));assert((days==std::vector<int>{2024,2028}));
 rules={};rules.repeat=Repeat::Daily;rules.count=3;rules.excludedCount=1;rules.excluded[0]=localEpoch({2026,10,10},10);days.clear();assert(occurrences(localEpoch({2026,10,9},10),localEpoch({2026,10,9},11),rules,localEpoch({2026,10,1}),localEpoch({2026,11,1}),[&](int64_t s,int64_t){days.push_back(localDate(s).day);return true;}));assert((days==std::vector<int>{9,11}));
 rules={};rules.repeat=Repeat::Daily;unsigned emitted=0;assert(occurrences(localEpoch({2020,1,1},9),localEpoch({2020,1,1},10),rules,localEpoch({2026,10,9}),localEpoch({2026,10,10}),[&](int64_t,int64_t){++emitted;return true;}));assert(emitted==1);
 // Cancelling expansion must not yield a successful partial query or hang on
 // old COUNT-limited series with no matching occurrence in the requested range.
 rules.count=10000;unsigned checks=0;emitted=0;
 assert(!occurrences(localEpoch({2000,1,1},9),localEpoch({2000,1,1},10),rules,localEpoch({2026,10,9}),localEpoch({2026,10,10}),[&](int64_t,int64_t){++emitted;return true;},[&](){return ++checks<4;}));
 assert(checks==4&&emitted==0);
 // MSVC's TZ parser does not implement POSIX transition rules. The firmware
 // uses newlib, so don't claim this host validates hardware/newlib DST behavior.
#ifndef _WIN32
 assert(localEpoch({2026,3,29},2,30)==0);
 assert(localEpoch({2026,3,30})-localEpoch({2026,3,29})==23*3600);
 assert(localEpoch({2026,10,26})-localEpoch({2026,10,25})==25*3600);
#endif
 rules.interval=0;assert(!rules.valid());rules.interval=1;rules.excludedCount=33;assert(!rules.valid());
}
