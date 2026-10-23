#pragma once
#include <cstring>
#include <cstddef>
namespace hp {
class LuaEventFilter {
public:
    static constexpr size_t capacity=8;
    void reset(){count_=0;explicit_=false;}
    void begin(){count_=0;explicit_=true;}
    bool add(const char* name){if(!name||!name[0]||std::strlen(name)>=48)return false;
        for(size_t i=0;i<count_;++i)if(!std::strcmp(names_[i],name))return true;
        if(count_==capacity)return false;std::strcpy(names_[count_++],name);return true;}
    bool accepts(const char* name,bool ownedJob=false)const{if(ownedJob||!explicit_)return true;
        for(size_t i=0;i<count_;++i)if(!std::strcmp(names_[i],name))return true;return false;}
    size_t count()const{return count_;}bool explicitSelection()const{return explicit_;}
private:char names_[capacity][48]{};size_t count_=0;bool explicit_=false;
};
}
