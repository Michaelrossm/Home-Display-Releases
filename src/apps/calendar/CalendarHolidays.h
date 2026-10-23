#pragma once
#include "CalendarDate.h"
#include <cstring>
#include <initializer_list>
namespace hp::calendar {
struct Region {char state[3]="BY";bool assumption=false,augsburg=false,corpusChristiMunicipality=false;};
inline bool validRegion(const char* state){for(const char* s:{"BW","BY","BE","BB","HB","HH","HE","MV","NI","NW","RP","SL","SN","ST","SH","TH"})if(state&&!strcmp(s,state))return true;return false;}
template<class Emit> inline void holidays(int year,const Region& region,Emit emit){
 auto is=[&](const char* s){return !strcmp(region.state,s);};
 auto fixed=[&](int m,int d,const char* title){emit(Date{year,m,d},title);};
 auto movable=[&](int offset,const char* title){const auto e=easter(year);emit(dateFromDay(dayNumber(year,e.month,e.day)+offset),title);};
 fixed(1,1,"Neujahr");movable(-2,"Karfreitag");movable(1,"Ostermontag");fixed(5,1,"Tag der Arbeit");
 movable(39,"Christi Himmelfahrt");movable(50,"Pfingstmontag");fixed(10,3,"Tag der Deutschen Einheit");fixed(12,25,"1. Weihnachtstag");fixed(12,26,"2. Weihnachtstag");
 if(is("BW")||is("BY")||is("ST"))fixed(1,6,"Heilige Drei Könige");
 if((is("BE")&&year>=2019)||(is("MV")&&year>=2023))fixed(3,8,"Internationaler Frauentag");
 if(is("BB")){movable(0,"Ostersonntag");movable(49,"Pfingstsonntag");}
 if(is("BW")||is("BY")||is("HE")||is("NW")||is("RP")||is("SL")||((is("SN")||is("TH"))&&region.corpusChristiMunicipality))movable(60,"Fronleichnam");
 if(is("BY")&&region.augsburg)fixed(8,8,"Augsburger Friedensfest");
 if(is("SL")||(is("BY")&&region.assumption))fixed(8,15,"Mariä Himmelfahrt");
 if(is("TH")&&year>=2019)fixed(9,20,"Weltkindertag");
 if(is("BB")||is("MV")||is("SN")||is("ST")||is("TH")||((is("HB")||is("HH")||is("NI")||is("SH"))&&year>=2018)||year==2017)fixed(10,31,"Reformationstag");
 if(is("BW")||is("BY")||is("NW")||is("RP")||is("SL"))fixed(11,1,"Allerheiligen");
 if(is("SN")){int d=22;while(weekday(year,11,d)!=2)--d;fixed(11,d,"Buß- und Bettag");}
 // One-off Berlin legislation; not repeated annually.
 if(is("BE")&&(year==2020||year==2025))fixed(5,8,"Tag der Befreiung");
}
}
