#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <lwip/sockets.h>
namespace hp {
struct MdnsDiscoveryResult { String service,hostname,host; uint16_t port=0; String product; };
int queryMdnsServices(const String* services,size_t count,MdnsDiscoveryResult* output,size_t capacity,uint32_t timeoutMs=1200,int* error=nullptr);
}
namespace hp { class MinimalMdns{public:static MinimalMdns&instance();bool start(const char*hostname,const IPAddress&ip,uint16_t httpPort=80);void stop();void loop();bool active()const{return active_;}private:bool openSocket();void sendProbe();void announce();void handlePacket(const uint8_t*,size_t,const sockaddr_in&);bool sendResponse(const sockaddr_in*target=nullptr,uint16_t id=0);int sock_=-1;bool active_=false,probing_=false;uint8_t probes_=0;uint32_t nextProbe_=0,nextAnnounce_=0;IPAddress ip_;char host_[64]{},fqdn_[72]{},instance_[72]{};uint16_t port_=80;uint8_t rx_[1500]{},tx_[1500]{};}; }
