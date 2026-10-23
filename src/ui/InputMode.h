#pragma once
#include <cstring>

namespace hp::keyboardInput {
enum class Mode { Text, Integer, Decimal, SignedDecimal, Phone, DateTime };
inline const char* accepted(Mode mode) {
    switch(mode) {
    case Mode::Integer: return "0123456789";
    case Mode::Decimal: return "0123456789.";
    case Mode::SignedDecimal: return "0123456789.-";
    case Mode::Phone: return "0123456789+*# ()-";
    case Mode::DateTime: return "0123456789-/:. ";
    default: return nullptr;
    }
}
inline Mode fromAccepted(const char* chars) {
    if(!chars || !*chars) return Mode::Text;
    bool digit=false;
    for(const char* p=chars;*p;++p) {
        if(*p>='0' && *p<='9') digit=true;
        else if(!std::strchr(".+-/: *#()",*p)) return Mode::Text;
    }
    if(!digit) return Mode::Text;
    if(std::strpbrk(chars,"+*#()")) return Mode::Phone;
    if(std::strpbrk(chars,"/: ")) return Mode::DateTime;
    if(std::strchr(chars,'-') && !std::strchr(chars,'.')) return Mode::DateTime;
    if(std::strchr(chars,'-')) return Mode::SignedDecimal;
    return std::strchr(chars,'.') ? Mode::Decimal : Mode::Integer;
}
inline bool parse(const char* name, Mode& mode) {
    if(!name || !std::strcmp(name,"text")) mode=Mode::Text;
    else if(!std::strcmp(name,"integer")) mode=Mode::Integer;
    else if(!std::strcmp(name,"number") || !std::strcmp(name,"decimal")) mode=Mode::Decimal;
    else if(!std::strcmp(name,"signed_decimal")) mode=Mode::SignedDecimal;
    else if(!std::strcmp(name,"phone")) mode=Mode::Phone;
    else if(!std::strcmp(name,"datetime")) mode=Mode::DateTime;
    else return false;
    return true;
}
}
