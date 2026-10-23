#include "MdnsService.h"
#include "MinimalMdns.h"
#include "NetworkService.h"
namespace hp {
MdnsService& MdnsService::instance(){static MdnsService m;return m;}
bool MdnsService::start(const char* host){
    const String address=NetworkService::instance().ip();if(on_&&hostname_==host&&address_==address)return true;
    IPAddress ip;if(!ip.fromString(address))return false;
    hostname_=host;address_=address;checkedAt_=millis();on_=MinimalMdns::instance().start(hostname_.c_str(),ip,80);return on_;
}
void MdnsService::stop(){MinimalMdns::instance().stop();on_=false;}
void MdnsService::loop(){
    if(!on_)return;
    if(uint32_t(millis()-checkedAt_)>=1000){checkedAt_=millis();const String address=NetworkService::instance().ip();
        if(address!=address_){IPAddress ip;if(!ip.fromString(address)){stop();return;}address_=address;on_=MinimalMdns::instance().start(hostname_.c_str(),ip,80);}}
    if(on_)MinimalMdns::instance().loop();
}
}
