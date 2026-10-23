#pragma once
#include <cstdint>
#include <ctime>
#ifdef _WIN32
inline tm* localtime_r(const time_t* value,tm* output){return localtime_s(output,value)==0?output:nullptr;}
#endif
namespace hp::calendar {
struct Date {int year=1970,month=1,day=1;};
inline bool leap(int y){return y%4==0&&(y%100!=0||y%400==0);}
inline int daysInMonth(int y,int m){static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};return m>=1&&m<=12?days[m-1]+(m==2&&leap(y)):0;}
inline bool validDate(int y,int m,int d){return y>=1900&&y<=2199&&d>=1&&d<=daysInMonth(y,m);}
// Gregorian civil date <-> Unix day. Independent of host/firmware timezone.
inline int64_t dayNumber(int y,int m,int d){
 y-=m<=2;const int era=(y>=0?y:y-399)/400;const unsigned yo=y-era*400;
 const unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 return era*146097LL+yo*365+yo/4-yo/100+doy-719468;
}
inline Date dateFromDay(int64_t z){
 z+=719468;const int era=(z>=0?z:z-146096)/146097;const unsigned doe=z-era*146097;
 const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;int y=yoe+era*400;
 const unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
 const int d=doy-(153*mp+2)/5+1,m=mp+(mp<10?3:-9);y+=m<=2;return {y,m,d};
}
inline int weekday(int y,int m,int d){int n=int((dayNumber(y,m,d)+3)%7);return n<0?n+7:n;} // Monday=0
inline int64_t localEpoch(Date date,int hour=0,int minute=0){
 if(!validDate(date.year,date.month,date.day)||hour<0||hour>23||minute<0||minute>59)return 0;
 tm value{};value.tm_year=date.year-1900;value.tm_mon=date.month-1;value.tm_mday=date.day;value.tm_hour=hour;value.tm_min=minute;value.tm_isdst=-1;
 const auto result=mktime(&value);
 // Reject nonexistent DST times, rather than silently moving an appointment.
 if(value.tm_year!=date.year-1900||value.tm_mon!=date.month-1||value.tm_mday!=date.day||value.tm_hour!=hour||value.tm_min!=minute)return 0;
 return int64_t(result);
}
inline Date localDate(int64_t epoch){time_t t=epoch;tm value{};localtime_r(&t,&value);return {value.tm_year+1900,value.tm_mon+1,value.tm_mday};}
inline Date easter(int y){const int a=y%19,b=y/100,c=y%100,d=b/4,e=b%4,f=(b+8)/25,g=(b-f+1)/3,h=(19*a+b-d-g+15)%30,i=c/4,k=c%4,l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451,n=h+l-7*m+114;return {y,n/31,n%31+1};}
enum class Repeat:uint8_t {None,Daily,Weekly,Monthly,Yearly};
struct Rules {
 Repeat repeat=Repeat::None;uint16_t interval=1,count=0;int64_t untilUtc=0;
 uint8_t excludedCount=0;int64_t excluded[32]{};int16_t reminderMinutes=-1;
 bool valid()const{return uint8_t(repeat)<=4&&interval>=1&&interval<=366&&count<=10000&&excludedCount<=32&&reminderMinutes>=-1&&reminderMinutes<=10080;}
};
// Preserve local wall-clock time through DST; invalid monthly/leap dates skip.
struct ContinueExpansion {bool operator()()const{return true;}};
template<class Emit,class Continue=ContinueExpansion> inline bool occurrences(int64_t start,int64_t end,const Rules& rules,int64_t from,int64_t to,Emit emit,Continue proceed={}){
 if(!rules.valid()||end<=start||to<=from)return false;
 if(!proceed())return false;
 if(rules.repeat==Repeat::None){if(end>from&&start<to)emit(start,end);return true;}
 time_t s=start,e=end;tm a{},b{};localtime_r(&s,&a);localtime_r(&e,&b);
 const auto base=dayNumber(a.tm_year+1900,a.tm_mon+1,a.tm_mday);
 const auto span=dayNumber(b.tm_year+1900,b.tm_mon+1,b.tm_mday)-base;
 unsigned seen=0,first=0;
 if(!rules.count&&from>start){
  const auto current=localDate(from);const int64_t days=dayNumber(current.year,current.month,current.day)-base-span-2;
  if(rules.repeat==Repeat::Daily||rules.repeat==Repeat::Weekly)first=days>0?days/(rules.interval*(rules.repeat==Repeat::Weekly?7:1)):0;
  else if(rules.repeat==Repeat::Monthly){const int months=(current.year-a.tm_year-1900)*12+current.month-a.tm_mon-1-2;first=months>0?months/rules.interval:0;}
  else {const int years=current.year-a.tm_year-1900-1;first=years>0?years/rules.interval:0;}
 }
 // 300 Gregorian years is a hard bound, independent of malformed input.
 for(unsigned index=first;index<110000;++index){
  // Check even while scanning old COUNT-limited series, before any emission.
  if((index-first)%32==0&&!proceed())return false;
  Date date{a.tm_year+1900,a.tm_mon+1,a.tm_mday};
  if(rules.repeat==Repeat::Daily||rules.repeat==Repeat::Weekly)date=dateFromDay(base+int64_t(index)*rules.interval*(rules.repeat==Repeat::Weekly?7:1));
  else if(rules.repeat==Repeat::Monthly){const int months=date.year*12+date.month-1+index*rules.interval;date.year=months/12;date.month=months%12+1;}
  else date.year+=index*rules.interval;
  if(date.year>2199)return true;if(!validDate(date.year,date.month,date.day))continue;
  const int64_t epoch=localEpoch(date,a.tm_hour,a.tm_min);if(!epoch)continue;const int64_t when=epoch+a.tm_sec;
  if(rules.untilUtc&&when>rules.untilUtc)return true;if(rules.count&&seen>=rules.count)return true;++seen;
  if(when>=to)return true;
  const auto finishDate=dateFromDay(dayNumber(date.year,date.month,date.day)+span);
  const auto finish=localEpoch(finishDate,b.tm_hour,b.tm_min)+b.tm_sec;
  if(finish<=from||finish<=when)continue;
  bool excluded=false;for(unsigned j=0;j<rules.excludedCount;++j)if(rules.excluded[j]==when)excluded=true;
  if(!excluded&&!emit(when,finish))return true;
 }
 return false;
}
}
