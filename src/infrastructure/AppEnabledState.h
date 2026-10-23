#pragma once
#include <string>
#include <map>

namespace hp {
// One NVS key, full identities in the value: no truncated-ID/hash collisions.
struct AppEnabledState {
    static constexpr const char* key="app_enabled_v1";
    static constexpr size_t maxBytes=3800; // below the NVS string-value limit
    std::map<std::string,bool> values;
    static bool validId(const std::string& id){
        if(id.empty()||id.size()>64)return false;
        for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
        return true;
    }
    bool decode(const std::string& data){
        values.clear();if(data.empty())return true;
        if(data.size()>maxBytes||data.compare(0,3,"v1\n"))return false;
        size_t start=3;
        while(start<data.size()){
            const size_t end=data.find('\n',start);
            if(end==std::string::npos||end<start+3||data[end-2]!='=')return false;
            const std::string id=data.substr(start,end-start-2);const char state=data[end-1];
            if(!validId(id)||(state!='0'&&state!='1')||!values.emplace(id,state=='1').second||values.size()>128)return false;
            start=end+1;
        }
        return true;
    }
    std::string encode() const {
        std::string out="v1\n";
        for(const auto& item:values)out+=item.first+(item.second?"=1\n":"=0\n");
        return out;
    }
};
}
