#pragma once
#include "CalendarDate.h"
#include <array>
#include <cstdio>
#include <cstring>
namespace hp::calendar {
struct MonthGridModel {
    struct Cell{Date date;char iso[11]{};bool inMonth=false,weekend=false;};
    std::array<Cell,42> cells{};
    bool set(int year,int month,bool sunday){
        if(!validDate(year,month,1))return false;
        const int mondayIndex=weekday(year,month,1);
        const int shift=sunday?(mondayIndex+1)%7:mondayIndex;
        const int64_t start=dayNumber(year,month,1)-shift;
        for(size_t i=0;i<cells.size();++i){auto& c=cells[i];c.date=dateFromDay(start+i);
            snprintf(c.iso,sizeof(c.iso),"%04d-%02d-%02d",c.date.year,c.date.month,c.date.day);
            c.inMonth=c.date.month==month;const int day=weekday(c.date.year,c.date.month,c.date.day);c.weekend=day>=5;
        }return true;
    }
    static bool validIso(const char* text){
        if(!text||strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
        for(unsigned i=0;i<10;++i)if(i!=4&&i!=7&&(text[i]<'0'||text[i]>'9'))return false;
        return validDate((text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0',(text[5]-'0')*10+text[6]-'0',(text[8]-'0')*10+text[9]-'0');
    }
};
}
