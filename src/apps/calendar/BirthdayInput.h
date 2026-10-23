#pragma once
#include <string>
#include <cstdio>
namespace hp::calendar {
inline std::string birthdayInput(const char* text,bool german){
    std::string digits,out;for(const char* p=text;*p&&digits.size()<8;++p)if(*p>='0'&&*p<='9')digits+=*p;
    for(size_t i=0;i<digits.size();++i){if(i==(german?2u:4u)||i==(german?4u:6u))out+='-';out+=digits[i];}return out;
}
inline std::string birthdayText(unsigned year,unsigned month,unsigned day,bool german){
    if(!month||!day)return {};char text[11];
    if(german)std::snprintf(text,sizeof(text),"%02u-%02u-%04u",day,month,year);
    else std::snprintf(text,sizeof(text),"%04u-%02u-%02u",year,month,day);return text;
}
inline bool parseBirthday(const char* text,bool german,int& year,int& month,int& day){
    const std::string s=text;if(s.size()!=10||s[german?2:4]!='-'||s[german?5:7]!='-')return false;
    for(size_t i=0;i<s.size();++i)if(i!=(german?2u:4u)&&i!=(german?5u:7u)&&(s[i]<'0'||s[i]>'9'))return false;
    int first,last;if(std::sscanf(text,german?"%2d-%2d-%4d":"%4d-%2d-%2d",&first,&month,&last)!=3)return false;
    year=german?last:first;day=german?first:last;return true;
}
}
