#pragma once
#include <Arduino.h>
namespace hp {
inline bool validPersonNamePart(const String& value,size_t limit){if(value.length()>limit)return false;for(const char* p=value.c_str();*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127)return false;return true;}
inline String personDisplayName(String first,String last){first.trim();last.trim();return first+(first.length()&&last.length()?" ":"")+last;}
// Legacy display names remain intact; don't guess cultural/multi-part name splits.
inline void legacyPersonName(const String& name,String& first,String& last){if(!first.length()&&!last.length())first=name;}
}
