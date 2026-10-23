#include "EthernetService.h"
#include "NetworkService.h"
#include "BoardConfig.h"
#include <ETH.h>
#include <Network.h>
#include <esp_eth_mac_esp.h>
namespace hp {
EthernetService& EthernetService::instance(){static EthernetService s;return s;}
void EthernetService::onEvent(arduino_event_t* event){if(!event)return;switch(event->event_id){case ARDUINO_EVENT_ETH_GOT_IP:NetworkService::instance().reportEthernet(true,ETH.localIP());break;case ARDUINO_EVENT_ETH_DISCONNECTED:case ARDUINO_EVENT_ETH_STOP:NetworkService::instance().reportEthernet(false);break;default:break;}}
bool EthernetService::begin(){if(started_)return true;Network.onEvent(onEvent);/* ESP-IDF emac_rmii_clock_mode_t: EMAC_CLK_EXT_IN is value 1; ETH.h omits its symbolic name on P4. GPIO50 is the dedicated input. */started_=ETH.begin(ETH_PHY_IP101,board::kEthernetPhyAddress,board::kEthernetMdc,board::kEthernetMdio,board::kEthernetReset,static_cast<eth_clock_mode_t>(1));return started_;}
}
