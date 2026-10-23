#include "MinimalMdns.h"
#include "BuildInfo.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <algorithm>
namespace hp {
namespace {constexpr uint16_t P=5353;constexpr char G[]="224.0.0.251";constexpr uint16_t A=1,PTR=12,TXT=16,SRV=33,ANY=255,IN=1,FLUSH=0x8000;constexpr uint32_t TTL=120,PTTL=4500;uint16_t r16(const uint8_t*p){return(p[0]<<8)|p[1];}void p16(uint8_t*p,uint16_t v){p[0]=v>>8;p[1]=v;}void p32(uint8_t*p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
struct B{uint8_t*d;size_t cap,n=12;uint16_t ans=0;B(uint8_t*x,size_t c,uint16_t id=0):d(x),cap(c){memset(d,0,12);p16(d,id);p16(d+2,0x8400);}bool room(size_t x){return n+x<=cap;}bool u8(uint8_t v){if(!room(1))return false;d[n++]=v;return true;}bool u16(uint16_t v){if(!room(2))return false;p16(d+n,v);n+=2;return true;}bool u32(uint32_t v){if(!room(4))return false;p32(d+n,v);n+=4;return true;}bool raw(const void*p,size_t z){if(!room(z))return false;memcpy(d+n,p,z);n+=z;return true;}bool name(const char*s){while(*s){const char*dot=strchr(s,'.');size_t l=dot?(size_t)(dot-s):strlen(s);if(!l||l>63||!u8(l)||!raw(s,l))return false;if(!dot)break;s=dot+1;}return u8(0);}bool head(const char*o,uint16_t t,uint16_t c,uint32_t ttl,size_t&rd){if(!name(o)||!u16(t)||!u16(c)||!u32(ttl)||!room(2))return false;rd=n;n+=2;return true;}bool fin(size_t rd,size_t st){if(n-st>65535)return false;p16(d+rd,n-st);++ans;return true;}bool a(const char*o,const IPAddress&ip){size_t rd;if(!head(o,A,IN|FLUSH,TTL,rd))return false;size_t st=n;uint8_t x[4]={ip[0],ip[1],ip[2],ip[3]};return raw(x,4)&&fin(rd,st);}bool ptr(const char*o,const char*t){size_t rd;if(!head(o,PTR,IN,PTTL,rd))return false;size_t st=n;return name(t)&&fin(rd,st);}bool srv(const char*o,uint16_t port,const char*t){size_t rd;if(!head(o,SRV,IN|FLUSH,TTL,rd))return false;size_t st=n;return u16(0)&&u16(0)&&u16(port)&&name(t)&&fin(rd,st);}bool txt(const char*o,const char*product,const char*ver){size_t rd;if(!head(o,TXT,IN|FLUSH,TTL,rd))return false;size_t st=n;char a[96];int l=snprintf(a,sizeof(a),"product=%s",product);if(l<0||l>255||!u8(l)||!raw(a,l))return false;l=snprintf(a,sizeof(a),"version=%s",ver);if(l<0||l>255||!u8(l)||!raw(a,l))return false;return fin(rd,st);}void done(){p16(d+6,ans);}};
bool readName(const uint8_t*d,size_t len,size_t&off,char*out,size_t os){if(!os||off>=len)return false;size_t c=off,w=0,j=0;bool jumped=false;while(c<len&&j<=12){uint8_t l=d[c++];if(!l){if(!jumped)off=c;out[w]=0;return true;}if((l&0xc0)==0xc0){if(c>=len)return false;uint16_t p=((l&0x3f)<<8)|d[c++];if(p>=len)return false;if(!jumped)off=c;c=p;jumped=true;++j;continue;}if((l&0xc0)||l>63||c+l>len)return false;if(w){if(w+1>=os)return false;out[w++]='.';}if(w+l>=os)return false;memcpy(out+w,d+c,l);w+=l;c+=l;}return false;}
}
MinimalMdns& MinimalMdns::instance(){static MinimalMdns m;return m;}
// Worker-only bounded DNS-SD queries use the responder's existing DNS codec.
int queryMdnsServices(const String* services,size_t count,MdnsDiscoveryResult* output,size_t capacity,uint32_t timeoutMs,int* error) {
    if(error)*error=0;
    if(!services||!output||count<1||count>4||capacity<1)return 0;
    capacity=std::min<size_t>(capacity,8);timeoutMs=std::min<uint32_t>(timeoutMs,1500);
    struct Record { char owner[192],target[192]; uint16_t type,port; uint32_t address; char product[96]; };
    auto* records=static_cast<Record*>(heap_caps_calloc(24,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!records){if(error)*error=1;return 0;}
    const int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0){if(error)*error=2;heap_caps_free(records);return 0;}
    sockaddr_in bindAddress{};bindAddress.sin_family=AF_INET;
    if(bind(fd,reinterpret_cast<sockaddr*>(&bindAddress),sizeof(bindAddress))<0){if(error)*error=2;close(fd);heap_caps_free(records);return 0;}
    fcntl(fd,F_SETFL,O_NONBLOCK);
    sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_port=htons(P);destination.sin_addr.s_addr=inet_addr(G);
    uint8_t packet[1500];size_t recordsCount=0;
    for(size_t i=0;i<count;++i) {
        B builder(packet,sizeof(packet),1);p16(packet+2,0);p16(packet+4,1);
        if(!builder.name(services[i].c_str())||!builder.u16(PTR)||!builder.u16(IN))continue;
        if(sendto(fd,packet,builder.n,0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))<0&&error)*error=2;
    }
    const uint32_t start=millis();unsigned packets=0;
    while(static_cast<uint32_t>(millis()-start)<timeoutMs&&packets<128) {
        int length=recv(fd,packet,sizeof(packet),0);if(length<=0){delay(3);continue;}++packets;
        if(length<12||!(r16(packet+2)&0x8000))continue;
        const size_t len=length;size_t offset=12;
        const uint16_t questions=r16(packet+4);
        if(questions>16)continue;
        bool valid=true;char name[192];
        for(unsigned i=0;i<questions;++i){if(!readName(packet,len,offset,name,sizeof(name))||offset+4>len){valid=false;break;}offset+=4;}
        if(!valid)continue;
        const unsigned answers=r16(packet+6)+r16(packet+8)+r16(packet+10);
        for(unsigned i=0;i<answers&&i<64;++i) {
            if(!readName(packet,len,offset,name,sizeof(name))||offset+10>len)break;
            const uint16_t type=r16(packet+offset),cls=r16(packet+offset+2)&0x7fff,size=r16(packet+offset+8);
            offset+=10;const size_t end=offset+size;if(end>len)break;
            Record record{};strlcpy(record.owner,name,sizeof(record.owner));record.type=type;
            if(cls!=IN){offset=end;continue;}
            size_t cursor=offset;
            if(type==PTR){if(!readName(packet,len,cursor,record.target,sizeof(record.target))||cursor>end){offset=end;continue;}}
            else if(type==SRV&&size>=7){record.port=r16(packet+offset+4);cursor+=6;if(!readName(packet,len,cursor,record.target,sizeof(record.target))||cursor>end){offset=end;continue;}}
            else if(type==A&&size==4)memcpy(&record.address,packet+offset,4);
            else if(type==TXT){
                // Only public product metadata; never forward arbitrary TXT
                // records (which can contain passwords or authentication data).
                while(cursor<end){const size_t length=packet[cursor++];if(length>end-cursor){valid=false;break;}
                    if(length>8&&!strncasecmp(reinterpret_cast<const char*>(packet+cursor),"product=",8)){
                        const size_t n=std::min(length-8,sizeof(record.product)-1);memcpy(record.product,packet+cursor+8,n);record.product[n]=0;}
                    cursor+=length;
                }
                if(!valid){offset=end;continue;}
            }
            else{offset=end;continue;}
            offset=end;bool duplicate=false;
            for(size_t j=0;j<recordsCount;++j)if(records[j].type==record.type&&!strcasecmp(records[j].owner,record.owner)&&!strcasecmp(records[j].target,record.target)){records[j]=record;duplicate=true;break;}
            if(!duplicate&&recordsCount<24)records[recordsCount++]=record;
        }
    }
    close(fd);int resultCount=0;
    for(size_t i=0;i<recordsCount&&resultCount<static_cast<int>(capacity);++i) {
        const auto& ptr=records[i];if(ptr.type!=PTR)continue;
        bool requested=false;for(size_t s=0;s<count;++s)if(!strcasecmp(ptr.owner,services[s].c_str()))requested=true;
        if(!requested)continue;
        for(size_t j=0;j<recordsCount;++j) {
            const auto& srv=records[j];if(srv.type!=SRV||!srv.port||strcasecmp(srv.owner,ptr.target))continue;
            for(size_t k=0;k<recordsCount;++k) {
                const auto& address=records[k];if(address.type!=A||strcasecmp(address.owner,srv.target))continue;
                const uint32_t ip=ntohl(address.address);const uint8_t a=ip>>24,b=(ip>>16)&255;
                if(!(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168)))continue;
                bool duplicate=false;
                for(int n=0;n<resultCount;++n)if(output[n].service==ptr.owner&&output[n].hostname==srv.target)duplicate=true;
                if(duplicate)continue;
                char host[INET_ADDRSTRLEN];inet_ntop(AF_INET,&address.address,host,sizeof(host));
                String product;
                for(size_t t=0;t<recordsCount;++t)if(records[t].type==TXT&&!strcasecmp(records[t].owner,srv.owner)){product=records[t].product;break;}
                output[resultCount++]={ptr.owner,srv.target,host,srv.port,product};break;
            }
            if(resultCount>=static_cast<int>(capacity))break;
        }
    }
    heap_caps_free(records);return resultCount;
}
bool MinimalMdns::openSocket(){sock_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(sock_<0)return false;int yes=1;setsockopt(sock_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(P);a.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(sock_,(sockaddr*)&a,sizeof(a))<0){stop();return false;}ip_mreq m{};m.imr_multiaddr.s_addr=inet_addr(G);m.imr_interface.s_addr=inet_addr(ip_.toString().c_str());if(setsockopt(sock_,IPPROTO_IP,IP_ADD_MEMBERSHIP,&m,sizeof(m))<0){stop();return false;}if(setsockopt(sock_,IPPROTO_IP,IP_MULTICAST_IF,&m.imr_interface,sizeof(m.imr_interface))<0){stop();return false;}int ttl=255;setsockopt(sock_,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl));fcntl(sock_,F_SETFL,O_NONBLOCK);return true;}
bool MinimalMdns::start(const char*h,const IPAddress&ip,uint16_t port){stop();if(!h||!h[0]||(ip[0]==0&&ip[1]==0&&ip[2]==0&&ip[3]==0))return false;strlcpy(host_,h,sizeof(host_));snprintf(fqdn_,sizeof(fqdn_),"%s.local",host_);snprintf(instance_,sizeof(instance_),"Home-Display %s",host_);ip_=ip;port_=port;if(!openSocket())return false;active_=probing_=true;probes_=0;nextProbe_=millis();return true;}
void MinimalMdns::stop(){if(sock_>=0){close(sock_);sock_=-1;}active_=probing_=false;}
void MinimalMdns::sendProbe(){if(sock_<0)return;memset(tx_,0,12);p16(tx_+4,1);B b(tx_,sizeof(tx_));p16(tx_+2,0);b.n=12;b.name(fqdn_);b.u16(ANY);b.u16(IN);p16(tx_+4,1);sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(P);to.sin_addr.s_addr=inet_addr(G);sendto(sock_,tx_,b.n,0,(sockaddr*)&to,sizeof(to));}
bool MinimalMdns::sendResponse(const sockaddr_in*target,uint16_t id){B b(tx_,sizeof(tx_),target?id:0);char httpType[]="_http._tcp.local",homeType[]="_homedisplay._tcp.local",httpInst[160],homeInst[160];snprintf(httpInst,sizeof(httpInst),"%s.%s",instance_,httpType);snprintf(homeInst,sizeof(homeInst),"%s.%s",instance_,homeType);if(!b.a(fqdn_,ip_)||!b.ptr(httpType,httpInst)||!b.srv(httpInst,port_,fqdn_)||!b.txt(httpInst,build::kProduct,build::kVersion)||!b.ptr(homeType,homeInst)||!b.srv(homeInst,port_,fqdn_)||!b.txt(homeInst,build::kProduct,build::kVersion))return false;b.done();sockaddr_in to{};if(target)to=*target;else{to.sin_family=AF_INET;to.sin_port=htons(P);to.sin_addr.s_addr=inet_addr(G);}return sendto(sock_,tx_,b.n,0,(sockaddr*)&to,sizeof(to))==(ssize_t)b.n;}
void MinimalMdns::announce(){sendResponse();nextAnnounce_=millis()+60000;}
void MinimalMdns::handlePacket(const uint8_t*d,size_t len,const sockaddr_in&from){if(len<12)return;uint16_t id=r16(d),flags=r16(d+2),qd=r16(d+4);if(flags&0x8000)return;size_t off=12;bool answer=false,requestedUnicast=false;for(uint16_t i=0;i<qd&&off<len;++i){char n[192];if(!readName(d,len,off,n,sizeof(n))||off+4>len)return;uint16_t type=r16(d+off),qclass=r16(d+off+2);off+=4;requestedUnicast|=(qclass&0x8000)!=0;if(!strcasecmp(n,fqdn_)||!strcasecmp(n,"_http._tcp.local")||!strcasecmp(n,"_homedisplay._tcp.local")||!strcasecmp(n,"_services._dns-sd._udp.local")||type==ANY)answer=true;}if(answer){bool unicast=id!=0||requestedUnicast||from.sin_port!=htons(P);sendResponse(unicast?&from:nullptr,id);}}
void MinimalMdns::loop(){if(!active_||sock_<0)return;uint32_t now=millis();if(probing_&&(int32_t)(now-nextProbe_)>=0){sendProbe();++probes_;if(probes_>=3){probing_=false;announce();}else nextProbe_=now+250;}else if(!probing_&&(int32_t)(now-nextAnnounce_)>=0)announce();for(int i=0;i<4;++i){sockaddr_in from{};socklen_t fl=sizeof(from);int n=recvfrom(sock_,rx_,sizeof(rx_),0,(sockaddr*)&from,&fl);if(n<=0)break;handlePacket(rx_,n,from);}}
}
