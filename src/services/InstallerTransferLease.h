#pragma once
#include <cstdint>
#include <cstring>
namespace hp {
// Protected by the existing luaUploadMutex. No timer/task; expiry is checked
// only between HTTP operations and never interrupts an in-flight commit.
struct InstallerTransferLease {
    static constexpr uint32_t timeoutMs=120000;
    char app[40]{},owner[65]{};uint32_t touched=0;
    static bool validOwner(const char* value){if(!value)return false;const size_t n=strlen(value);if(n<16||n>64)return false;for(size_t i=0;i<n;++i){const char c=value[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;}return true;}
    bool claim(const char* id,const char* token,uint32_t now){
        if(!id||!id[0]||strlen(id)>=sizeof(app)||!validOwner(token))return false;
        if(app[0]&&uint32_t(now-touched)>timeoutMs){app[0]=owner[0]=0;}
        if(app[0]&&(strcmp(app,id)||strcmp(owner,token)))return false;
        strcpy(app,id);strcpy(owner,token);touched=now;return true;
    }
    bool release(const char* id,const char* token){if(!app[0])return true;if(!id||!token||strcmp(app,id)||strcmp(owner,token))return false;app[0]=owner[0]=0;return true;}
};
}
