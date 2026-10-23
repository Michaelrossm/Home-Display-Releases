#pragma once
#include "CalendarDate.h"
#include <string>
#include <vector>
#include <algorithm>

namespace hp::calendar {
struct ImportedEvent {
    std::string uid,title,location,notes;
    int64_t start=0,end=0;
    bool allDay=false;
};
// Server-expanded RFC4791 calendar-data. Reject unsupported timezones/series
// rather than silently converting an appointment to the wrong time.
inline bool parseExpandedCalendar(const std::string& source,std::vector<ImportedEvent>& output,
                                  std::string& error,size_t limit=128) {
    if(source.size()>65536||limit>128){error="ical_size_limit";return false;}
    std::vector<std::string> lines;size_t pos=0;
    while(pos<source.size()){
        const auto end=source.find('\n',pos);std::string line=source.substr(pos,end==std::string::npos?std::string::npos:end-pos);
        pos=end==std::string::npos?source.size():end+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(!line.empty()&&(line[0]==' '||line[0]=='\t')){if(lines.empty()){error="ical_folding";return false;}lines.back()+=line.substr(1);}
        else {if(lines.size()>=2048){error="ical_line_limit";return false;}lines.push_back(std::move(line));}
        if(!lines.empty()&&lines.back().size()>8192){error="ical_property_limit";return false;}
    }
    auto unescape=[](const std::string& text){std::string out;out.reserve(text.size());for(size_t i=0;i<text.size();++i){if(text[i]=='\\'&&i+1<text.size()){char next=text[++i];out+=next=='n'||next=='N'?'\n':next;}else out+=text[i];}return out;};
    auto timestamp=[](const std::string& value,bool date,int64_t& result){
        if(value.size()!=(date?8:16)||(!date&&(value[8]!='T'||value[15]!='Z')))return false;
        for(size_t i=0;i<value.size();++i)if(i!=8&&i!=15&&(value[i]<'0'||value[i]>'9'))return false;
        auto n=[&](unsigned at,unsigned count){int v=0;while(count--)v=v*10+value[at++]-'0';return v;};
        const Date d{n(0,4),n(4,2),n(6,2)};if(!validDate(d.year,d.month,d.day))return false;
        if(date){result=localEpoch(d);return result>0;}
        const int h=n(9,2),m=n(11,2),s=n(13,2);if(h>23||m>59||s>59)return false;
        result=dayNumber(d.year,d.month,d.day)*86400+h*3600+m*60+s;return result>0;
    };
    bool calendar=false,ended=false,inEvent=false,cancelled=false,endDate=false;unsigned nested=0;ImportedEvent current;int durationDays=0,durationSeconds=0;
    for(const auto& line:lines){
        if(line=="BEGIN:VCALENDAR"){if(calendar||ended){error="ical_structure";return false;}calendar=true;continue;}
        if(line=="END:VCALENDAR"){if(!calendar||inEvent){error="ical_structure";return false;}calendar=false;ended=true;continue;}
        if(line=="BEGIN:VEVENT"){if(!calendar||inEvent){error="ical_structure";return false;}inEvent=true;nested=0;cancelled=endDate=false;current={};durationDays=durationSeconds=0;continue;}
        if(line=="END:VEVENT"){if(!inEvent||nested){error="ical_structure";return false;}inEvent=false;
            if(cancelled)continue;
            if(current.uid.empty()||current.start<=0){error="ical_required_property";return false;}
            if(current.end && endDate!=current.allDay){error="ical_datetime_kind";return false;}
            if(current.allDay&&durationSeconds){error="ical_date_duration";return false;}
            if(!current.end&&(durationDays||durationSeconds)){
                if(!current.allDay)current.end=current.start+int64_t(durationDays)*86400+durationSeconds;
                else {const auto d=localDate(current.start);current.end=localEpoch(dateFromDay(dayNumber(d.year,d.month,d.day)+durationDays));}
            }
            if(!current.end&&current.allDay){const auto d=localDate(current.start);current.end=localEpoch(dateFromDay(dayNumber(d.year,d.month,d.day)+1));}
            if(current.end<=current.start){error="ical_end_or_duration_unsupported";return false;}
            bool duplicate=false;for(const auto& prior:output)if(prior.uid==current.uid&&prior.start==current.start){if(prior.end!=current.end||prior.title!=current.title||prior.location!=current.location||prior.notes!=current.notes||prior.allDay!=current.allDay){error="ical_identity_conflict";return false;}duplicate=true;}
            if(!duplicate){if(output.size()>=limit){error="ical_event_limit";return false;}output.push_back(std::move(current));}continue;
        }
        if(!inEvent)continue;
        if(line.compare(0,6,"BEGIN:")==0){++nested;continue;}
        if(line.compare(0,4,"END:")==0){if(!nested){error="ical_component";return false;}--nested;continue;}
        if(nested)continue;
        const auto colon=line.find(':');if(colon==std::string::npos){error="ical_property";return false;}
        const auto semicolon=line.find(';');const auto key=line.substr(0,std::min(colon,semicolon));const auto value=line.substr(colon+1);
        const auto parameters=line.substr(key.size(),colon-key.size());
        if(key=="RRULE"||key=="RDATE"||key=="EXDATE"){error="ical_server_expansion_required";return false;}
        if(key=="UID"){if(!current.uid.empty()||value.size()>1024){error="ical_uid";return false;}current.uid=value;}
        else if(key=="DTSTART"||key=="DTEND"){
            const bool date=parameters==";VALUE=DATE";if(!parameters.empty()&&!date){error="ical_timezone_unsupported";return false;}
            int64_t parsed=0;if(!timestamp(value,date,parsed)){error="ical_datetime";return false;}
            if(key=="DTSTART"){if(current.start){error="ical_duplicate_start";return false;}current.start=parsed;current.allDay=date;}
            else {if(current.end||durationDays||durationSeconds){error="ical_duplicate_end";return false;}current.end=parsed;endDate=date;}
        }else if(key=="DURATION"){
            if(value.empty()||value[0]!='P'||current.end||durationDays||durationSeconds){error="ical_duration";return false;}
            uint64_t n=0;bool time=false,digits=false;unsigned seen=0,last=0;
            for(size_t i=1;i<value.size();++i){const char c=value[i];if(c>='0'&&c<='9'){if(n>3162240){error="ical_duration";return false;}n=n*10+c-'0';digits=true;continue;}
                if(c=='T'&&!time&&!digits){time=true;continue;}if(!digits){error="ical_duration";return false;}
                unsigned rank=0;uint64_t seconds=0;
                if(!time&&(c=='D'||c=='W')){rank=c=='W'?1:2;seconds=n*(c=='W'?604800ULL:86400ULL);}
                else if(time&&(c=='H'||c=='M'||c=='S')){rank=c=='H'?3:c=='M'?4:5;seconds=n*(c=='H'?3600ULL:c=='M'?60ULL:1ULL);}
                else {error="ical_duration";return false;}
                if(rank<=last||(seen&2)||(rank==1&&seen)||seconds>366ULL*86400||uint64_t(durationDays)*86400+durationSeconds+seconds>366ULL*86400){error="ical_duration";return false;}
                last=rank;seen|=1U<<rank;
                if(!time)durationDays+=seconds/86400;else durationSeconds+=seconds;
                n=0;digits=false;
            }if(digits||(!durationDays&&!durationSeconds)||value.back()=='T'){error="ical_duration";return false;}
        }else if(key=="SUMMARY")current.title=unescape(value);
        else if(key=="LOCATION")current.location=unescape(value);
        else if(key=="DESCRIPTION")current.notes=unescape(value);
        else if(key=="STATUS"&&value=="CANCELLED")cancelled=true;
    }
    if(calendar||inEvent||!ended){error="ical_structure";return false;}return true;
}
} // namespace hp::calendar
