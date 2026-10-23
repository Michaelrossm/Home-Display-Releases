#pragma once
#include <stddef.h>
#include <string.h>

namespace hp {
// Canonical package IDs only. Never decode path separators or filesystem paths.
inline bool parseAppWebRoute(const char* uri,char (&id)[32],bool& trailingSlash) {
    id[0]=0;trailingSlash=false;
    if(!uri||strncmp(uri,"/apps/",6))return false;
    const char* p=uri+6;size_t n=0;
    while(p[n]&&p[n]!='/'&&p[n]!='?') {
        const char c=p[n];
        if(n>=31||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
        ++n;
    }
    if(!n)return false;
    const char* end=p+n;
    if(*end=='/'){trailingSlash=true;++end;}
    if(*end&&*end!='?')return false;
    memcpy(id,p,n);id[n]=0;return true;
}
}
