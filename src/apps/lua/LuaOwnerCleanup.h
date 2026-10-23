#pragma once
#include <cstddef>
namespace hp {
// Registration and invocation are UI/startup-only. No heap, duplicates or replacement.
class LuaOwnerCleanup {
public:
    using Handler=void(*)(const char*);
    static constexpr size_t capacity=8;
    bool add(Handler handler){if(!handler)return false;for(auto h:handlers_)if(h==handler)return true;
        for(auto& h:handlers_)if(!h){h=handler;return true;}return false;}
    void run(const char* owner)const{if(!owner||!owner[0])return;for(auto h:handlers_)if(h)h(owner);}
private:Handler handlers_[capacity]{};
};
}
