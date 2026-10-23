#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace hp {
struct DirectoryPageCache {
    static constexpr unsigned pages=4,items=100;
    struct Revision {uint32_t storage=0,usb=0,nas=0;bool operator==(const Revision& b)const{return storage==b.storage&&usb==b.usb&&nas==b.nas;}};
    struct Item{char name[97]{};uint64_t size=0,modified=0;bool directory=false;};
    struct Page{char key[1025]{};Revision revision;Item entries[items]{};unsigned count=0;uint32_t saved=0,used=0;bool more=false,valid=false;};
    Page slots[pages]{};uint32_t clock=0;
    Page* get(const char* key,Revision revision,uint32_t now,uint32_t ttl){
        for(auto& p:slots)if(p.valid&&!strcmp(p.key,key)&&p.revision==revision&&uint32_t(now-p.saved)<ttl){p.used=++clock;return &p;}return nullptr;
    }
    Page* replace(const char* key,Revision revision,uint32_t now){
        if(!key||strlen(key)>1024)return nullptr;
        auto* target=&slots[0];for(auto& p:slots){if(p.valid&&!strcmp(p.key,key)){target=&p;break;}if(!p.valid||p.used<target->used)target=&p;}
        target->valid=false;strcpy(target->key,key);target->revision=revision;target->saved=now;target->used=++clock;return target;
    }
};
}
