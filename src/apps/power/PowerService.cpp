#include "PowerService.h"
#include "../../services/EnergyService.h"
#include "../../services/EnergyHistoryService.h"
#include "../../services/DiagnosticsService.h"
#include "../../services/SettingsService.h"
#include "../../services/NetworkService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/PowerManager.h"
#include "../../services/EnergyHistoryImport.h"
#include "../../services/ConfigService.h"
#include <algorithm>

namespace hp {

PowerService& PowerService::instance(){static PowerService p;return p;}
namespace {uint32_t ownerHash(const char* owner){uint32_t hash=2166136261u;for(unsigned i=0;owner&&owner[i]&&i<32;++i)hash=(hash^static_cast<uint8_t>(owner[i]))*16777619u;return hash?hash:1;}}
bool PowerService::bindSource(const String& owner,unsigned index){
 String host,password;if(!energyTrackerCredentials(owner,index,host,password)||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(20))!=pdTRUE)return false;
 if(sourceOwner_.length()&&sourceOwner_!=owner&&!sourceCancelled_){xSemaphoreGive(mux_);return false;}
 if(sourceOwner_==owner&&sourceIndex_==index&&!sourceCancelled_){xSemaphoreGive(mux_);return true;}
 sourceOwner_=owner;sourceIndex_=index;sourceOwnerHash_=ownerHash(owner.c_str());sourceCancelled_=false;sourceChanged_=true;xSemaphoreGive(mux_);return true;
}
void PowerService::cleanupOwner(const char* owner,bool suspending){if(!suspending&&sourceOwnerHash_==ownerHash(owner))sourceCancelled_=true;}
bool PowerService::source(String& host,String& password){
 String owner;unsigned index=0;
 if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){owner=sourceOwner_;index=sourceIndex_;xSemaphoreGive(mux_);}
 if(owner.length()&&!sourceCancelled_)return energyTrackerCredentials(owner,index,host,password);
 if(owner.length()&&sourceCancelled_&&mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){
  if(sourceCancelled_){sourceOwner_="";sourceIndex_=0;sourceOwnerHash_=0;sourceCancelled_=false;}
  xSemaphoreGive(mux_);
 }
 const auto settings=SettingsService::instance().snapshot();host=settings.powerHost;password=settings.powerPassword;return host.length();
}

bool PowerService::begin(){
    if(!mux_)mux_=xSemaphoreCreateMutex();
    EnergyHistoryService::instance().setHistoryProvider(this);
    return mux_&&BackgroundScheduler::instance().add(this);
}

bool PowerService::supportsHistoryBackfill()const{
    PowerMeterData current;
    return const_cast<PowerService*>(this)->trySnapshot(current)&&current.nativeStatus;
}

bool PowerService::fetchHistory(int64_t from,int64_t to,uint16_t,const Visitor& visitor){
    if(!NetworkService::instance().online()||!supportsHistoryBackfill())return false;
    String host,password;if(!source(host,password))return false;
    return client_.fetchDetailHistory(host,password,from,to,visitor);
}

PowerMeterData PowerService::snapshot(){
    PowerMeterData x;
    if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){x=data_;xSemaphoreGive(mux_);}
    return x;
}

PowerMeterHistoryView PowerService::history(){
    PowerMeterHistoryView x;
    if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(40))==pdTRUE){x=hist_;xSemaphoreGive(mux_);}
    return x;
}

bool PowerService::trySnapshot(PowerMeterData& out){
    if(!mux_||xSemaphoreTake(mux_,0)!=pdTRUE)return false;
    out=data_;xSemaphoreGive(mux_);return true;
}

bool PowerService::tryHistory(PowerMeterHistoryView& out){
    if(!mux_||xSemaphoreTake(mux_,0)!=pdTRUE)return false;
    out=hist_;xSemaphoreGive(mux_);return true;
}

bool PowerService::requestDiscovery(){
    if(!mux_||!NetworkService::instance().online()||xSemaphoreTake(mux_,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    if(discoveryActive_){xSemaphoreGive(mux_);return false;}
    discoveryActive_=true;
    discoveryStatus_="Suche nach IR-Tracker und kompatiblen Schnittstellen ...";
    xSemaphoreGive(mux_);
    if(NetworkWorker::instance().submit(discoveryJob,this,"ir-discover"))return true;
    if(xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){
        discoveryActive_=false;
        discoveryStatus_="Netzwerk-Worker ist ausgelastet.";
        xSemaphoreGive(mux_);
    }
    return false;
}

void PowerService::discoveryJob(void*p){static_cast<PowerService*>(p)->executeDiscovery();}

void PowerService::executeDiscovery(){
    PowerDiscoveryDevice found[8]{};
    const int n=PowerDiscovery::discover(found,8);
    if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(100))==pdTRUE){
        memcpy(discovered_,found,sizeof(found));
        discoveryActive_=false;
        discoveryStatus_=n?String(n)+" kompatible Geraete gefunden; Auswahl antippen und speichern.":"Keine kompatiblen Geraete gefunden.";
        xSemaphoreGive(mux_);
    }
}

int PowerService::copyDiscovered(PowerDiscoveryDevice*out,int cap){
    if(!out||cap<1||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
    int n=0;for(;n<cap&&n<8&&discovered_[n].host[0];++n)out[n]=discovered_[n];
    xSemaphoreGive(mux_);return n;
}

int PowerService::tryCopyDiscovered(PowerDiscoveryDevice*out,int cap){
    if(!out||cap<1||!mux_||xSemaphoreTake(mux_,0)!=pdTRUE)return 0;
    int n=0;for(;n<cap&&n<8&&discovered_[n].host[0];++n)out[n]=discovered_[n];
    xSemaphoreGive(mux_);return n;
}

String PowerService::discoveryStatus(){
    if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(20))!=pdTRUE)return "Status nicht verfuegbar.";
    String s=discoveryStatus_;xSemaphoreGive(mux_);return s;
}

bool PowerService::tryDiscoveryStatus(String& s){
    if(!mux_||xSemaphoreTake(mux_,0)!=pdTRUE)return false;
    s=discoveryStatus_;xSemaphoreGive(mux_);return true;
}

void PowerService::backgroundTick(){
    if(!NetworkService::instance().online())return;
    if(sourceChanged_.exchange(false)){last_=lastHist_=0;pollIntervalMs_=3000;}

    const uint32_t now=millis();
    const bool pollDue=!last_||static_cast<uint32_t>(now-last_)>=pollIntervalMs_;

    // The scheduler calls this blocking job every second. Nothing in this
    // service needs a complete NVS/Settings snapshot between 3 s meter polls.
    // History is hourly and is evaluated whenever a meter poll is due.
    if(!pollDue)return;

    const uint32_t ownerVersion=sourceOwnerHash_.load(),configVersion=ConfigService::instance().revision();
    String host,password;
    if(!source(host,password)){
        last_=now;
        if(sourceOwnerHash_){PowerMeterData empty;EnergyService::instance().publishPower(empty);}
        return;
    }

    last_=now;
    PerformanceGuard boost;
    PowerMeterData x;
    if(!client_.poll(host,x,password)){
        DiagnosticsService::instance().recordHttpFailure();
        pollIntervalMs_=std::min<uint32_t>(60000,pollIntervalMs_*2);
    }else pollIntervalMs_=3000;

    if(sourceCancelled_||ownerVersion!=sourceOwnerHash_.load()||configVersion!=ConfigService::instance().revision())return;
    if(xSemaphoreTake(mux_,pdMS_TO_TICKS(100))==pdTRUE){
        data_=x;
        xSemaphoreGive(mux_);
        EnergyService::instance().publishPower(x);
    }

    if(password.length()&&
       (!lastHist_||static_cast<uint32_t>(now-lastHist_)>=3600000UL)){
        lastHist_=now;
        PowerMeterHistoryView h;
        String err;
        if(client_.loadHistory(host,password,h,err)&&
           xSemaphoreTake(mux_,pdMS_TO_TICKS(250))==pdTRUE){
            hist_=h;
            xSemaphoreGive(mux_);
        }
    }
}

}
