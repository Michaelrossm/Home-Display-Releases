#include "DockShortcutService.h"
#include "ConfigService.h"
#include "../core/AppRegistry.h"
#include <cstring>

namespace hp {
DockShortcutService& DockShortcutService::instance() { static DockShortcutService s; return s; }

bool DockShortcutService::validId(const char* id) {
    if(!id || !*id || strlen(id) >= kIdLength) return false;
    for(const char* p=id; *p; ++p) {
        const char c=*p;
        if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_')) return false;
    }
    return true;
}
String DockShortcutService::savedList() {
    auto& cfg=ConfigService::instance();
    return cfg.has(kKey) ? cfg.getString(kKey, "") : String(kDefaults);
}
bool DockShortcutService::hasId(const String& list, const char* id) {
    if(!validId(id)) return false;
    size_t start=0;
    while(start<list.length()) {
        int end=list.indexOf('|',start);
        if(end<0)end=list.length();
        if(list.substring(start,end)==id) return true;
        start=static_cast<size_t>(end)+1;
    }
    return false;
}
String DockShortcutService::without(const String& list, const char* id) {
    String out; out.reserve(list.length());
    size_t start=0;
    while(start<list.length()) {
        int end=list.indexOf('|',start);
        if(end<0)end=list.length();
        const String token=list.substring(start,end);
        if(validId(token.c_str())&&token!=id) {
            if(out.length())out+='|';
            out+=token;
        }
        start=static_cast<size_t>(end)+1;
    }
    return out;
}
size_t DockShortcutService::load(char ids[][kIdLength], size_t capacity) const {
    if(!ids||!capacity)return 0;
    const String list=savedList();
    size_t count=0,start=0;
    const size_t limit=capacity<kMaxItems?capacity:kMaxItems;
    while(start<list.length()&&count<limit) {
        int end=list.indexOf('|',start);
        if(end<0)end=list.length();
        const String token=list.substring(start,end);
        if(validId(token.c_str())) {
            bool duplicated=false;
            for(size_t i=0;i<count;++i)if(token==ids[i]){duplicated=true;break;}
            if(!duplicated)strlcpy(ids[count++],token.c_str(),kIdLength);
        }
        start=static_cast<size_t>(end)+1;
    }
    return count;
}
bool DockShortcutService::add(const char* id) {
    if(!validId(id))return false;
    AppRegistryEntry entry;
    if(!AppRegistry::instance().lookup(id,entry)||!entry.launcherVisible||entry.state!=AppRegistryState::Enabled)return false;
    String list=savedList();
    if(hasId(list,id))return true; // No flash write on duplicate drop.
    // Revalidate the existing list and cap the total number of shortcuts.
    char ids[kMaxItems][kIdLength]{};
    const size_t count=load(ids,kMaxItems);
    String normalized;
    size_t retained=0;
    for(size_t i=0;i<count;++i){
        AppRegistryEntry existing;
        if(!AppRegistry::instance().lookup(ids[i],existing)||!existing.launcherVisible)continue;
        // Disabled apps retain their shortcut; uninstalled apps cannot consume
        // all slots forever. Neither case changes app enable/delete state.
        if(normalized.length())normalized+='|';normalized+=ids[i];++retained;
    }
    if(retained>=kMaxItems)return false;
    if(normalized.length())normalized+='|';
    normalized+=id;
    return ConfigService::instance().putString(kKey,normalized);
}
bool DockShortcutService::remove(const char* id) {
    if(!validId(id))return false;
    const String list=savedList();
    if(!hasId(list,id))return false;
    String result=without(list,id);
    // ConfigService needs a non-empty string write result. Use an
    // invalid ID as the empty-list marker, not "-" (a valid app ID).
    if(!result.length())result="/";
    return ConfigService::instance().putString(kKey,result);
}
}
