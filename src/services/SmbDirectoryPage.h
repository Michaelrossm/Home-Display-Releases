#pragma once
#include <cstddef>
#include <cstdint>
namespace hp {
// SMB2 FILE_ID_FULL_DIRECTORY_INFORMATION. Pure bounded wire decoder; no heap.
class SmbDirectoryPage {
public:
    using Visitor=bool(*)(const char*,uint64_t,uint64_t,bool,void*);
    static uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
    static uint64_t u64(const uint8_t* p){return u32(p)|(uint64_t(u32(p+4))<<32);}
    static bool visit(const uint8_t* bytes,size_t size,Visitor visitor,void* context,bool& stopped){
        stopped=false;if(!bytes||!visitor||!size||size>8192)return false;size_t offset=0;
        for(;;){const size_t remaining=size-offset;if(remaining<80)return false;const uint8_t* p=bytes+offset;
            const uint32_t next=u32(p),length=u32(p+60);
            if(length>510||(length&1)||length>remaining-80||(next&&(next<80+length||next>remaining||(next&7))))return false;
            char name[1021];size_t used=0;bool valid=true;
            for(size_t i=0;i<length;i+=2){uint32_t c=p[80+i]|(uint32_t(p[81+i])<<8);
                if(c>=0xd800&&c<=0xdbff){if(i+3>=length){valid=false;break;}const uint32_t low=p[82+i]|(uint32_t(p[83+i])<<8);
                    if(low<0xdc00||low>0xdfff){valid=false;break;}c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;
                }else if(c>=0xdc00&&c<=0xdfff){valid=false;break;}
                if(c<32||c=='/'||c=='\\'||c==':'){valid=false;break;}
                if(c<128)name[used++]=char(c);
                else if(c<2048){name[used++]=char(0xc0|(c>>6));name[used++]=char(0x80|(c&63));}
                else if(c<65536){name[used++]=char(0xe0|(c>>12));name[used++]=char(0x80|((c>>6)&63));name[used++]=char(0x80|(c&63));}
                else{ name[used++]=char(0xf0|(c>>18));name[used++]=char(0x80|((c>>12)&63));name[used++]=char(0x80|((c>>6)&63));name[used++]=char(0x80|(c&63));}
            }
            name[used]=0;const uint64_t ticks=u64(p+24),epoch=116444736000000000ULL;
            if(valid&&used&&!visitor(name,u64(p+40),ticks>=epoch?(ticks-epoch)/10000000:0,(u32(p+56)&0x10)!=0,context)){stopped=true;return true;}
            if(!next)return true;offset+=next;
        }
    }
};
}
