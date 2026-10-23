#pragma once
#include <cctype>
#include <cstring>
namespace hp {
// Deterministic German-friendly list ordering, without a second contact index.
inline unsigned phonebookSortUnit(const char*& text){
    unsigned value=static_cast<unsigned char>(*text++);
    if(value==0xc3&&*text){const unsigned next=static_cast<unsigned char>(*text);
        unsigned folded=next>=0x80&&next<=0x9e&&next!=0x97?next+0x20:next;
        if(folded==0xa4||folded==0xb6||folded==0xbc||folded==0x9f){++text;return folded==0xa4?'a':folded==0xb6?'o':folded==0xbc?'u':'s';}}
    return static_cast<unsigned char>(std::tolower(value));
}
inline bool phonebookNameLess(const char* left,const char* right){
    const char* a=left;const char* b=right;
    while(*a&&*b){const unsigned x=phonebookSortUnit(a),y=phonebookSortUnit(b);if(x!=y)return x<y;}
    if(*a||*b)return !*a;
    return strcmp(left,right)<0;
}
inline unsigned char phonebookFold(const char* current,const char* previous){
    const auto ch=static_cast<unsigned char>(*current);
    if(previous&&static_cast<unsigned char>(*previous)==0xc3&&ch>=0x80&&ch<=0x9e&&ch!=0x97)return ch+0x20;
    return static_cast<unsigned char>(std::tolower(ch));
}
inline bool phonebookContains(const char* text,const char* query){
    if(!*query)return true;
    const char* begin=text;
    for(;*text;++text){const char* a=text;const char* b=query;
        while(*a&&*b&&phonebookFold(a,a>begin?a-1:nullptr)==phonebookFold(b,b>query?b-1:nullptr)){++a;++b;}
        if(!*b)return true;
    }
    return false;
}
template<class Contact> bool phonebookMatches(const Contact& c,const char* user,const char* query){
    if(*user&&strcmp(c.userId.c_str(),user))return false;
    for(const auto& v:c.phones)if(phonebookContains(v.value.c_str(),query))return true;
    for(const auto& v:c.emails)if(phonebookContains(v.value.c_str(),query))return true;
    for(const auto& a:c.addresses)if(phonebookContains(a.street.c_str(),query)||phonebookContains(a.city.c_str(),query)||phonebookContains(a.postalCode.c_str(),query))return true;
    return phonebookContains(c.company.c_str(),query)||phonebookContains(c.street.c_str(),query)||phonebookContains(c.city.c_str(),query)||
        (phonebookContains(c.name.c_str(),query)||phonebookContains(c.email.c_str(),query)||
         phonebookContains(c.phone.c_str(),query)||phonebookContains(c.mobile.c_str(),query));
}
}
