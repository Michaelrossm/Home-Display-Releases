#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace hp {
static_assert(sizeof(double)==8,"Calculator requires 64-bit double");
// UI-independent, bounded basic calculator. Unlike pinned Lua, double is 64 bit.
class CalculatorModel {
public:
    void clear() { entry_="0"; history_.clear(); accumulator_=lastRhs_=0; pending_=lastOp_=0; fresh_=true; error_=false; }
    const std::string& entry() const { return entry_; }
    const std::string& history() const { return history_; }
    bool hasMemory() const {return memory_!=0;}
    std::string display() const { auto text=entry_; for(auto& c:text)if(c=='.')c=',';return text; }
    void press(const char* key) {
        if(!key)return;
        if(!std::strcmp(key,"AC")){clear();return;}
        if(error_)clear();
        if(!std::strcmp(key,"MC")){memory_=0;return;}
        if(!std::strcmp(key,"MR")){set(memory_);fresh_=true;return;}
        if(!std::strcmp(key,"M+")||!std::strcmp(key,"M-")){
            const double value=std::strtod(entry_.c_str(),nullptr);
            const double next=memory_+(key[1]=='+'?value:-value);
            if(std::isfinite(next))memory_=next;else set(NAN);
            return;
        }
        if((key[0]&&std::strspn(key,"0123456789")==std::strlen(key))||!std::strcmp(key,",")) {
            if(fresh_){entry_="0";fresh_=false;if(!pending_){history_.clear();lastOp_=0;}}
            if(!std::strcmp(key,",")){if(entry_.find('.')==std::string::npos)entry_+='.';return;}
            for(const char* p=key;*p;++p){
                size_t digits=0;for(char c:entry_)if(c>='0'&&c<='9')++digits;
                if(entry_=="0")entry_=*p;else if(digits<14)entry_+=*p;
            }
            return;
        }
        if(!std::strcmp(key,"DEL")){if(fresh_)return;if(entry_.size()>1)entry_.pop_back();else entry_="0";if(entry_=="-")entry_="0";return;}
        double value=std::strtod(entry_.c_str(),nullptr);
        if(!std::strcmp(key,"+/-")){if(value!=0){if(entry_[0]=='-')entry_.erase(0,1);else entry_.insert(0,"-");}return;}
        if(!std::strcmp(key,"%")){set(pending_=='+'||pending_=='-'?accumulator_*value/100:value/100);return;}
        if(!std::strcmp(key,"sqrt")){set(value<0?NAN:std::sqrt(value));fresh_=true;return;}
        if(!std::strcmp(key,"x^2")){set(value*value);fresh_=true;return;}
        if(!std::strcmp(key,"1/x")){set(value==0?NAN:1/value);fresh_=true;return;}
        const char op=!std::strcmp(key,"x")?'*':key[0];
        if(op=='=') {
            const char operation=pending_?pending_:lastOp_;
            if(!operation)return;
            const double rhs=pending_?value:lastRhs_;
            const double lhs=pending_?accumulator_:value;
            history_=format(lhs)+" "+operation+" "+format(rhs)+" =";
            set(calculate(lhs,rhs,operation));lastOp_=operation;lastRhs_=rhs;pending_=0;fresh_=true;return;
        }
        if(op=='+'||op=='-'||op=='*'||op=='/') {
            if(pending_&&!fresh_){set(calculate(accumulator_,value,pending_));if(error_)return;value=std::strtod(entry_.c_str(),nullptr);}
            accumulator_=value;pending_=op;lastOp_=0;fresh_=true;history_=format(value)+" "+op;
        }
    }
private:
    std::string entry_="0",history_;
    double accumulator_=0,lastRhs_=0,memory_=0;
    char pending_=0,lastOp_=0;
    bool fresh_=true,error_=false;
    static std::string format(double value){char text[48];std::snprintf(text,sizeof(text),"%.14g",value==0?0:value);return text;}
    void set(double value){error_=!std::isfinite(value);entry_=error_?"Fehler":format(value);}
    static double calculate(double a,double b,char op){switch(op){case '+':return a+b;case '-':return a-b;case '*':return a*b;case '/':return b==0?NAN:a/b;default:return NAN;}}
};
}
