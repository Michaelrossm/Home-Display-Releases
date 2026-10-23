#include "RadioService.h"
#include "../../services/SecurityService.h"
#include "RadioSettings.h"
#include "../../core/NumericUtils.h"
#include "../../services/DiagnosticsService.h"
#include "../../services/MediaSessionService.h"
#include "../../services/NetworkService.h"
#include "../../services/PowerManager.h"
#include "../../core/EventLog.h"
namespace hp {
RadioService& RadioService::instance(){ static RadioService r; return r; }
bool RadioService::begin(){ if(!mux_)mux_=xSemaphoreCreateMutex(); auto s=RadioSettings::instance().load(); client_.configure(s.host,s.pin); if(!mediaSourceId_)mediaSourceId_=MediaSessionService::instance().registerSource(MediaSourceType::Radio,"Radio",mediaThunk); return mux_!=nullptr&&mediaSourceId_!=0&&BackgroundScheduler::instance().add(this); }
bool RadioService::mediaThunk(MediaCommand c){ return instance().command(c); }
RadioState RadioService::snapshot(uint32_t waitMs,bool* valid){ RadioState x;if(valid)*valid=false; if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))==pdTRUE){x=state_;if(x.online&&commandError_.length())x.error=commandError_;if(valid)*valid=true; xSemaphoreGive(mux_);} return x; }
bool RadioService::command(MediaCommand c){
 return queue(Request::Command,static_cast<int>(c));
}
bool RadioService::setVolume(int value){return queue(Request::Volume,constrain(value,0,100));}
bool RadioService::requestModes(){return queue(Request::Modes);}
bool RadioService::requestSelectMode(int key){return queue(Request::SelectMode,key);}
bool RadioService::requestPresets(){return queue(Request::Presets);}
bool RadioService::requestSelectPreset(int key){return queue(Request::SelectPreset,key);}
bool RadioService::requestBrowseRoot(){return queue(Request::BrowseRoot);}
bool RadioService::requestBrowseSelect(size_t index){return queue(Request::BrowseSelect,static_cast<int>(index));}
bool RadioService::requestBrowseBack(){return queue(Request::BrowseBack);}
bool RadioService::requestReconfigure(){
 // Configuration changes must not be lost behind a running catalog/discovery.
 if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(10))!=pdTRUE)return false;
 reconfigurePending_=true;pending_=Request::None;catalogBusy_=true;
 xSemaphoreGive(mux_);return true;
}
bool RadioService::requestAutoDiscover(){return queue(Request::Discover);}
int RadioService::copyDiscovered(RadioDiscoveryDevice*target,int cap,uint32_t waitMs){if(!target||cap<1||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return 0;const int n=min(cap,discoveredCount_);for(int i=0;i<n;++i)target[i]=discovered_[i];xSemaphoreGive(mux_);return n;}
String RadioService::discoveryStatus(uint32_t waitMs){if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return "Suche nicht verfuegbar";const String status=discoveryStatus_;xSemaphoreGive(mux_);return status;}
bool RadioService::queue(Request request,int value){
 if(SecurityService::instance().tabletLocked())return false;
 auto cfg=RadioSettings::instance().load();
 if(cfg.parentalEnabled){
  const bool administrative=request==Request::Reconfigure||request==Request::Discover||request==Request::Modes||request==Request::Presets;
  if(cfg.lockDisplayControls&&!administrative)return false;
  const bool sourceChange=request==Request::SelectMode||request==Request::SelectPreset||request==Request::BrowseSelect||request==Request::BrowseBack;
  if(cfg.lockSources&&sourceChange)return false;
  if(request==Request::Command){auto c=static_cast<MediaCommand>(value);if(cfg.lockSources&&(c==MediaCommand::Next||c==MediaCommand::Previous))return false;if(cfg.lockPowerOff&&(c==MediaCommand::PowerToggle||c==MediaCommand::Stop)&&snapshot().power)return false;}
 }
 if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(10))!=pdTRUE)return false;
 // Only idempotent volume SETs coalesce. Never replace transport/source commands.
 if(pending_==Request::Volume&&request==Request::Volume){pendingValue_=value;xSemaphoreGive(mux_);return true;}
 if(pending_!=Request::None||catalogBusy_){xSemaphoreGive(mux_);return false;}pending_=request;pendingValue_=value;catalogBusy_=request!=Request::Command&&request!=Request::Volume;xSemaphoreGive(mux_);return true;}
int RadioService::copyModes(RadioMode*target,int cap,uint32_t waitMs){if(!target||cap<1||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return 0;int n=min(cap,modeCount_);for(int i=0;i<n;++i)target[i]=modes_[i];xSemaphoreGive(mux_);return n;}
int RadioService::copyPresets(RadioPreset*target,int cap,uint32_t waitMs){if(!target||cap<1||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return 0;int n=min(cap,presetCount_);for(int i=0;i<n;++i)target[i]=presets_[i];xSemaphoreGive(mux_);return n;}
int RadioService::copyBrowse(RadioNavItem*target,int cap,uint32_t waitMs){if(!target||cap<1||!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return 0;int n=min(cap,browseCount_);for(int i=0;i<n;++i)target[i]=browse_[i];xSemaphoreGive(mux_);return n;}
bool RadioService::catalogBusy(uint32_t waitMs){if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return true;bool b=catalogBusy_;xSemaphoreGive(mux_);return b;}
uint32_t RadioService::catalogRevision(uint32_t waitMs){if(!mux_||xSemaphoreTake(mux_,pdMS_TO_TICKS(waitMs))!=pdTRUE)return 0;uint32_t r=catalogRevision_;xSemaphoreGive(mux_);return r;}
void RadioService::updateCatalog(Request request,int value){
 int count=0;
 switch(request){
 case Request::Discover: {if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);discoveredCount_=0;discoveryStatus_="Suche nach UNDOK/FSAPI-Radios ...";xSemaphoreGive(mux_);}RadioDiscoveryDevice devices[8];const auto settings=RadioSettings::instance().load();const int found=RadioDiscovery::discover(devices,8,settings.pin);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);discoveredCount_=max(0,found);for(int i=0;i<discoveredCount_;++i)discovered_[i]=devices[i];discoveryStatus_=found?String(found)+" Radio(s) gefunden.":"Kein UNDOK/FSAPI-Radio gefunden.";xSemaphoreGive(mux_);}break;}
 case Request::Reconfigure: {
  auto settings=RadioSettings::instance().load();client_.configure(settings.host,settings.pin);
  if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);state_=RadioState{};modeCount_=presetCount_=browseCount_=0;xSemaphoreGive(mux_);}
  break;
 }
 case Request::Modes: {RadioMode values[16];count=client_.loadModes(values,16);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);modeCount_=max(0,count);for(int i=0;i<modeCount_;++i)modes_[i]=values[i];xSemaphoreGive(mux_);}break;}
 case Request::SelectMode: client_.setMode(value); break;
 case Request::Presets: {RadioPreset values[32];count=client_.loadPresets(values,32);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);presetCount_=max(0,count);for(int i=0;i<presetCount_;++i)presets_[i]=values[i];xSemaphoreGive(mux_);}break;}
 case Request::SelectPreset: client_.selectPreset(value); break;
 case Request::BrowseRoot: if(client_.navBegin()){RadioNavItem values[48];count=client_.loadNavAll(values,48);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);browseCount_=max(0,count);for(int i=0;i<browseCount_;++i)browse_[i]=values[i];xSemaphoreGive(mux_);}}break;
 case Request::BrowseSelect: {RadioNavItem item;bool valid=false;if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);if(value>=0&&value<browseCount_){item=browse_[value];valid=true;}xSemaphoreGive(mux_);}if(valid&&client_.navSelect(item)){RadioNavItem values[48];count=client_.loadNavAll(values,48);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);browseCount_=max(0,count);for(int i=0;i<browseCount_;++i)browse_[i]=values[i];xSemaphoreGive(mux_);}}break;}
 case Request::BrowseBack: if(client_.navBack()){RadioNavItem values[48];count=client_.loadNavAll(values,48);if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);browseCount_=max(0,count);for(int i=0;i<browseCount_;++i)browse_[i]=values[i];xSemaphoreGive(mux_);}}break;
 default: break;}
 if(mux_){xSemaphoreTake(mux_,portMAX_DELAY);catalogBusy_=reconfigurePending_;++catalogRevision_;xSemaphoreGive(mux_);}
}

void RadioService::executeCommand(MediaCommand c){
 const auto s=snapshot();const auto cfg=RadioSettings::instance().load();
 if(cfg.parentalEnabled&&(cfg.lockDisplayControls||
    (cfg.lockSources&&(c==MediaCommand::Next||c==MediaCommand::Previous))||
    (cfg.lockPowerOff&&(c==MediaCommand::Stop||(c==MediaCommand::PowerToggle&&s.power)))))return;
 const bool mayStart=(c==MediaCommand::PlayPause&&s.playStatus!=2)||(c==MediaCommand::PowerToggle&&!s.power);
 // Audio focus can involve other sources: acquire only on the central worker.
 if(mayStart&&!MediaSessionService::instance().preparePlayback(mediaSourceId_))return;
 const int cap=cfg.volumeLimitEnabled?max(1,numeric::percentToValueFloor(cfg.maxVolumePercent,s.volumeSteps)):s.volumeSteps;
 switch(c){
 case MediaCommand::Stop:client_.playControl(2);client_.setPower(false);MediaSessionService::instance().releaseAudioFocus(mediaSourceId_);break;
 case MediaCommand::PlayPause:client_.playControl(s.playStatus==2?2:1);break;
 case MediaCommand::Next:client_.playControl(3);break;
 case MediaCommand::Previous:client_.playControl(4);break;
 case MediaCommand::PowerToggle:if(client_.setPower(!s.power)&&s.power)MediaSessionService::instance().releaseAudioFocus(mediaSourceId_);break;
 case MediaCommand::MuteToggle:client_.setMute(!s.mute);break;
 case MediaCommand::VolumeUp:client_.setVolume(min(s.volume+1,cap));break;
 case MediaCommand::VolumeDown:client_.setVolume(max(0,s.volume-1));break;
 }
 const String failure=client_.lastError();
 const String message=failure.length()?(String(c==MediaCommand::Next?"Nächster Titel":c==MediaCommand::Previous?"Vorheriger Titel":"Radiobefehl")+": "+failure):String();
 if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){commandError_=message;xSemaphoreGive(mux_);}
 if(message.length())EventLog::instance().add("WARN","RADIO_COMMAND_FAILED",message);
}

void RadioService::backgroundTick(){
 Request request=Request::None;int value=0;if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){request=reconfigurePending_?Request::Reconfigure:pending_;value=pendingValue_;pending_=Request::None;reconfigurePending_=false;xSemaphoreGive(mux_);}
 if(SecurityService::instance().tabletLocked()&&request!=Request::None&&request!=Request::Reconfigure){if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){catalogBusy_=false;xSemaphoreGive(mux_);}request=Request::None;}
 if(request==Request::Reconfigure){updateCatalog(request,value);request=Request::None;}
 if(!NetworkService::instance().online()) {
  if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(20))==pdTRUE){
   state_.online=false;state_.error="Netzwerk offline";
   if(request==Request::Discover)discoveryStatus_="Netzwerk offline";
   if(request!=Request::None){catalogBusy_=false;++catalogRevision_;}
   xSemaphoreGive(mux_);
  }
  return;
 }
 if(request!=Request::None){PerformanceGuard boost;if(request==Request::Command){executeCommand(static_cast<MediaCommand>(value));}else if(request==Request::Volume){auto s=snapshot();auto cfg=RadioSettings::instance().load();const int requested=numeric::percentToValueRounded(value,max(1,s.volumeSteps));const int cap=cfg.volumeLimitEnabled?max(1,numeric::percentToValueFloor(cfg.maxVolumePercent,s.volumeSteps)):s.volumeSteps;client_.setVolume(min(requested,cap));}else updateCatalog(request,value);}
 if(millis()-last_<1500&&request==Request::None) return; last_=millis(); PerformanceGuard boost; RadioState x=snapshot(); bool ok=client_.pollCore(x); if(!ok) DiagnosticsService::instance().recordHttpFailure(); if(ok&&x.power) client_.pollNowPlaying(x);auto cfg=RadioSettings::instance().load();if(ok&&cfg.volumeLimitEnabled&&x.volumeSteps>0){const int cap=max(1,numeric::percentToValueFloor(cfg.maxVolumePercent,x.volumeSteps));if(x.volume>cap){client_.setVolume(cap);x.volume=cap;}}if(mux_&&xSemaphoreTake(mux_,pdMS_TO_TICKS(100))==pdTRUE){state_=x;xSemaphoreGive(mux_);} MediaState m; m.type=MediaSourceType::Radio; m.available=x.online;m.powered=x.power;m.playing=x.playStatus==2;m.muted=x.mute;m.volume=x.volume;m.volumeMax=x.volumeSteps;m.title=x.station.length()?x.station:x.artist;m.subtitle=x.text.length()?x.text:x.album;m.source=x.friendlyName;MediaSessionService::instance().update(mediaSourceId_,m);
}
}
