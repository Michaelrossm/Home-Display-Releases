#include "RegionService.h"
#include "ConfigService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include <ArduinoJson.h>
#include <cstring>
namespace hp {namespace {
struct Country{const char* code;const char* name;const char* english;};
constexpr Country countries[]={
 {"DE","Deutschland","Germany"},{"AT","Österreich","Austria"},{"CH","Schweiz","Switzerland"},
 {"FR","Frankreich","France"},{"IT","Italien","Italy"},{"ES","Spanien","Spain"},{"PT","Portugal","Portugal"},
 {"NL","Niederlande","Netherlands"},{"BE","Belgien","Belgium"},{"LU","Luxemburg","Luxembourg"},
 {"LI","Liechtenstein","Liechtenstein"},{"GB","Vereinigtes Königreich","United Kingdom"},{"IE","Irland","Ireland"},
 {"DK","Dänemark","Denmark"},{"SE","Schweden","Sweden"},{"NO","Norwegen","Norway"},{"FI","Finnland","Finland"},
 {"IS","Island","Iceland"},{"PL","Polen","Poland"},{"CZ","Tschechien","Czech Republic"},{"SK","Slowakei","Slovakia"},
 {"HU","Ungarn","Hungary"},{"SI","Slowenien","Slovenia"},{"HR","Kroatien","Croatia"},{"GR","Griechenland","Greece"},
 {"RO","Rumänien","Romania"},{"BG","Bulgarien","Bulgaria"},{"EE","Estland","Estonia"},{"LV","Lettland","Latvia"},
 {"LT","Litauen","Lithuania"},{"RS","Serbien","Serbia"},{"BA","Bosnien und Herzegowina","Bosnia and Herzegovina"},
 {"MK","Nordmazedonien","North Macedonia"},{"MT","Malta","Malta"},{"CY","Zypern","Cyprus"},{"AD","Andorra","Andorra"},
 {"MC","Monaco","Monaco"},{"SM","San Marino","San Marino"},{"VA","Vatikanstadt","Vatican City"},
 {"UA","Ukraine","Ukraine"},{"TR","Türkei","Turkey"},{"US","USA","United States"},{"CA","Kanada","Canada"},
 {"AU","Australien","Australia"},{"NZ","Neuseeland","New Zealand"},{"JP","Japan","Japan"},{"CN","China","China"},
 {"KR","Südkorea","South Korea"},{"IN","Indien","India"},{"SG","Singapur","Singapore"},{"MY","Malaysia","Malaysia"},
 {"TH","Thailand","Thailand"},{"PH","Philippinen","Philippines"},{"ID","Indonesien","Indonesia"},
 {"BR","Brasilien","Brazil"},{"AR","Argentinien","Argentina"},{"MX","Mexiko","Mexico"},{"CL","Chile","Chile"},
 {"CO","Kolumbien","Colombia"},{"ZA","Südafrika","South Africa"},{"MA","Marokko","Morocco"},{"EG","Ägypten","Egypt"},
 {"IL","Israel","Israel"},{"PK","Pakistan","Pakistan"},{"BD","Bangladesch","Bangladesh"},{"LK","Sri Lanka","Sri Lanka"},
 {"NP","Nepal","Nepal"},{"RU","Russland","Russia"},{"BY","Belarus","Belarus"},{"FO","Färöer","Faroe Islands"},
 {"GL","Grönland","Greenland"},{"RE","Réunion","Reunion"},{"GP","Guadeloupe","Guadeloupe"},{"MQ","Martinique","Martinique"}
};
}
RegionService& RegionService::instance(){static RegionService service;return service;}
size_t RegionService::count(){return sizeof(countries)/sizeof(*countries);}
const char* RegionService::code(size_t i){return countries[i<count()?i:0].code;}
const char* RegionService::name(size_t i){return countries[i<count()?i:0].name;}
int RegionService::find(const char* value){if(!value||!*value)return -1;String text(value);text.trim();
    if(text.equalsIgnoreCase("Great Britain"))return find("GB");
    for(size_t i=0;i<count();++i)if(text.equalsIgnoreCase(code(i))||text.equalsIgnoreCase(name(i))||text.equalsIgnoreCase(countries[i].english))return i;
    return -1;
}
void RegionService::begin(){const int i=find(ConfigService::instance().getString("region_country","DE").c_str());selected_=i<0?0:i;}
bool RegionService::request(size_t i){if(i>=count()||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    uint16_t idle=UINT16_MAX;if(!pending_.compare_exchange_strong(idle,uint16_t(i)))return false;
    if(i==selected()){state_=2;pending_=UINT16_MAX;return true;}
    state_=1;if(NetworkWorker::instance().submitLocalTransfer(save,this,"region-save"))return true;
    state_=3;pending_=UINT16_MAX;return false;
}
void RegionService::save(void* context){auto& self=*static_cast<RegionService*>(context);const auto i=self.pending_.load();
    SecurityWriteGuard guard;const bool ok=SecurityService::instance().settingsAllowed()&&!SecurityService::instance().tabletLocked()&&ConfigService::instance().putString("region_country",code(i));
    if(ok)self.selected_=i;self.state_=ok?2:3;self.pending_=UINT16_MAX;
}
const char* RegionService::status()const{switch(state_.load()){case 1:return "Region wird gespeichert …";case 2:return "Region gespeichert";case 3:return "Region konnte nicht gespeichert werden";default:return "Standard für Orts- und Postleitzahlsuche";}}
String RegionService::catalogJson()const{JsonDocument out;out["version"]=1;out["country"]=country();out["busy"]=busy();out["status"]=status();auto list=out["countries"].to<JsonArray>();for(size_t i=0;i<count();++i){auto row=list.add<JsonObject>();row["code"]=code(i);row["name"]=name(i);}String body;serializeJson(out,body);return body;}
}
