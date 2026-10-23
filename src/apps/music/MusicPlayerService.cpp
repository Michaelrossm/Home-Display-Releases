#include "MusicPlayerService.h"
#include "../../services/AudioHardwareService.h"
#include "../../services/AudioFileStream.h"
#include "../../services/FileBrowserService.h"
#include "../../services/ConfigService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/SettingsService.h"
#include "../../services/SecurityService.h"
#include "../../services/UsbStorageService.h"
#include "../../core/SystemSupervisor.h"
#include <audio_player.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <algorithm>
namespace hp {
struct MusicPlayerService::Action{enum Type{Scan,Play,Command,Volume,Options}type=Scan;size_t index=0;MediaCommand command=MediaCommand::Stop;String uri,title,json;std::vector<String> queue;bool retainQueue=false;};
MusicPlayerService& MusicPlayerService::instance(){static MusicPlayerService service;return service;}
bool MusicPlayerService::begin(){state_.volume=SettingsService::instance().snapshot().music.volume;rememberedVolume_=state_.volume;
 mediaSourceId_=MediaSessionService::instance().registerSource(MediaSourceType::LocalFile,"Home-Display Musik",mediaCommandThunk);
 EventBus::instance().subscribe(eventThunk);return mediaSourceId_&&BackgroundScheduler::instance().add(this);}
bool MusicPlayerService::mediaCommandThunk(MediaCommand c){return instance().command(c);}
void MusicPlayerService::eventThunk(const Event& e){if(e.type==EventType::MediaLibraryChanged)instance().libraryDirty_=true;}
MusicSnapshot MusicPlayerService::snapshot(){std::lock_guard<std::mutex> lock(mutex_);return state_;}
bool MusicPlayerService::track(size_t i,MusicTrack& out){std::lock_guard<std::mutex> lock(mutex_);const auto* t=library_?library_->at(i):nullptr;if(!t)return false;out=*t;return true;}
String MusicPlayerService::preferences(){std::lock_guard<std::mutex> lock(mutex_);return preferences_;}
void MusicPlayerService::error(const char* text){{std::lock_guard<std::mutex> lock(mutex_);state_.state=MusicState::Error;strlcpy(state_.error,text,sizeof(state_.error));++state_.revision;}MediaSessionService::instance().releaseAudioFocus(mediaSourceId_);publishMedia();}
bool MusicPlayerService::enqueue(Action* action){if(!action)return false;const bool stop=action->type==Action::Command&&action->command==MediaCommand::Stop;if((SecurityService::instance().tabletLocked()&&!stop)||busy_.exchange(true)){delete action;return false;}
 const bool queued=action->type==Action::Scan?NetworkWorker::instance().submitLocalTransfer(actionWorker,action,"music-scan"):NetworkWorker::instance().submitLocal(actionWorker,action,"music-action",true);
 if(!queued){busy_=false;delete action;return false;}return true;}
void MusicPlayerService::actionWorker(void* p){std::unique_ptr<Action> a(static_cast<Action*>(p));auto& s=instance();s.execute(*a);s.busy_=false;}
bool MusicPlayerService::rescan(){const auto s=snapshot();if(s.state==MusicState::Playing||s.state==MusicState::Paused||s.state==MusicState::Loading)return false;return enqueue(new(std::nothrow) Action);}
bool MusicPlayerService::playIndex(size_t index){MusicTrack t;if(!track(index,t))return false;auto* a=new(std::nothrow) Action;if(!a)return false;a->type=Action::Play;a->index=index;a->uri=t.path;a->title=t.title;return enqueue(a);}
bool MusicPlayerService::playUri(const String& uri,const String& title){if(uri.length()>512||(!uri.startsWith("http://")&&!uri.startsWith("https://")))return false;auto* a=new(std::nothrow) Action;if(!a)return false;a->type=Action::Play;a->uri=uri;a->title=title;return enqueue(a);}
bool MusicPlayerService::playQueue(const std::vector<String>& queue,size_t index){if(queue.empty()||queue.size()>32||index>=queue.size())return false;for(const auto& uri:queue)if(uri.length()>512||(!uri.startsWith("http://")&&!uri.startsWith("https://")&&!FileBrowserService::validUri(uri)))return false;auto* a=new(std::nothrow) Action;if(!a)return false;a->type=Action::Play;a->queue=queue;a->index=index;a->uri=queue[index];a->title=a->uri.substring(a->uri.lastIndexOf('/')+1);return enqueue(a);}
void MusicPlayerService::setVolume(uint8_t value){auto* a=new(std::nothrow) Action;if(!a)return;a->type=Action::Volume;a->index=std::min<unsigned>(100,value);enqueue(a);}
bool MusicPlayerService::command(MediaCommand command){if(command==MediaCommand::Stop){stopRequested_=true;std::lock_guard<std::mutex> lock(mutex_);if(cancellation_)cancellation_->store(true);if(busy_)return true;}auto* a=new(std::nothrow) Action;if(!a)return false;a->type=Action::Command;a->command=command;return enqueue(a);}
bool MusicPlayerService::saveOptions(const String& json){if(json.length()>6000)return false;JsonDocument d;if(deserializeJson(d,json)||!d.is<JsonObject>())return false;
 for(const char* key:{"favorites","folders","stations","playlists"})if(d[key].is<JsonArray>()&&d[key].size()>32)return false;
 auto validUri=[](const String& uri){if(!uri.length()||uri.length()>512||uri.indexOf('\r')>=0||uri.indexOf('\n')>=0)return false;if(uri.startsWith("http://")||uri.startsWith("https://")){const int start=uri.indexOf("://")+3;int end=uri.indexOf('/',start);if(end<0)end=uri.length();return uri.substring(start,end).indexOf('@')<0;}return FileBrowserService::validUri(uri);};
 for(JsonVariant v:d["favorites"].as<JsonArray>())if(!v.is<const char*>()||!validUri(v.as<String>()))return false;
 for(JsonVariant v:d["folders"].as<JsonArray>())if(!v.is<const char*>()||!FileBrowserService::validUri(v.as<String>()))return false;
 for(JsonVariant v:d["stations"].as<JsonArray>())if(!v["name"].is<const char*>()||!v["name"].as<String>().length()||v["name"].as<String>().length()>64||!validUri(v["url"].as<String>()))return false;
 for(JsonVariant v:d["playlists"].as<JsonArray>()){if(!v["name"].is<const char*>()||!v["name"].as<String>().length()||v["name"].as<String>().length()>64||!v["uris"].is<JsonArray>()||v["uris"].size()>32)return false;for(JsonVariant uri:v["uris"].as<JsonArray>())if(!validUri(uri.as<String>()))return false;}
 auto* a=new(std::nothrow) Action;if(!a)return false;a->type=Action::Options;a->json=json;return enqueue(a);}
bool MusicPlayerService::favorite(const String& uri){JsonDocument d;if(deserializeJson(d,preferences()))return false;for(JsonVariant v:d["favorites"].as<JsonArray>())if(v.as<String>()==uri)return true;return false;}
bool MusicPlayerService::toggleFavorite(const String& uri){JsonDocument d;if(deserializeJson(d,preferences()))return false;auto values=d["favorites"].is<JsonArray>()?d["favorites"].as<JsonArray>():d["favorites"].to<JsonArray>();bool removed=false;
 for(size_t i=0;i<values.size();++i)if(values[i].as<String>()==uri){values.remove(i);removed=true;break;}if(!removed){if(values.size()>=32)return false;values.add(uri);}String json;serializeJson(d,json);return saveOptions(json);}
bool MusicPlayerService::ensureDecoder(){if(decoder_)return true;if(!AudioHardwareService::instance().ready())return false;
 audio_player_config_t config{};config.priority=4;config.coreID=0;config.force_stereo=true;
 config.mute_fn=[](AUDIO_PLAYER_MUTE_SETTING){return ESP_OK;};
 config.clk_set_fn=[](uint32_t rate,uint32_t bits,i2s_slot_mode_t channels){return AudioHardwareService::setFormat(rate,bits,channels==I2S_SLOT_MODE_MONO?1:2);};
 config.write_fn=[](void* data,size_t bytes,size_t* written,uint32_t timeout){*written=AudioHardwareService::writePcm(data,bytes,timeout);return *written==bytes?ESP_OK:ESP_FAIL;};
 if(audio_player_new(config)!=ESP_OK)return false;
 audio_player_callback_register([](audio_player_cb_ctx_t* event){instance().audioEvents_.fetch_or(1u<<unsigned(event->audio_event));},nullptr);decoder_=true;return true;
}
void MusicPlayerService::execute(Action& a){
 if(SecurityService::instance().tabletLocked()&&!(a.type==Action::Command&&a.command==MediaCommand::Stop))return;
 if(a.type==Action::Scan){
  {std::lock_guard<std::mutex> lock(mutex_);state_.scanning=true;++state_.revision;}
  auto library=std::make_shared<MusicLibrary>();const bool ok=library->begin()&&library->scan();
  const String stored=ConfigService::instance().getString("music_prefs","{}");
  {std::lock_guard<std::mutex> lock(mutex_);if(ok){library_=library;state_.count=library->count();}preferences_=stored;JsonDocument d;deserializeJson(d,stored);state_.shuffle=d["shuffle"]|false;state_.repeat=d["repeat"]|false;state_.scanning=false;++state_.revision;}
 }else if(a.type==Action::Options){
  if(!ConfigService::instance().putString("music_prefs",a.json)){error("Einstellungen konnten nicht gespeichert werden");return;}
  {std::lock_guard<std::mutex> lock(mutex_);preferences_=a.json;JsonDocument d;deserializeJson(d,preferences_);state_.shuffle=d["shuffle"]|false;state_.repeat=d["repeat"]|false;++state_.revision;}
 }else if(a.type==Action::Volume){
  const uint8_t v=a.index;AudioHardwareService::instance().setVolume(v);auto s=SettingsService::instance().snapshot();s.music.volume=v;SettingsService::instance().save(s);
  {std::lock_guard<std::mutex> lock(mutex_);state_.volume=v;++state_.revision;if(v)rememberedVolume_=v;}
 }else if(a.type==Action::Play){
  if(stopRequested_)return;
  if(!a.retainQueue){queue_=a.queue;queueIndex_=a.index;}
  if(!MediaSessionService::instance().preparePlayback(mediaSourceId_)||!ensureDecoder()){error("Audio-Ausgabe nicht verfügbar");return;}
  if(cancellation_)cancellation_->store(true);audio_player_stop();
  auto cancel=std::make_shared<std::atomic<bool>>(false);FILE* input=AudioFileStream::open(a.uri,cancel);
  if(!input||audio_player_play(input)!=ESP_OK){if(input)fclose(input);error("Musikquelle konnte nicht geöffnet werden");return;}
  {std::lock_guard<std::mutex> lock(mutex_);cancellation_=cancel;currentUri_=a.uri;state_.index=a.index;state_.queueCount=queue_.size();state_.queueIndex=queueIndex_;state_.state=MusicState::Loading;state_.error[0]=0;
   strlcpy(state_.title,a.title.c_str(),sizeof(state_.title));strlcpy(state_.subtitle,a.uri.startsWith("http")?"Internetradio":a.uri.c_str(),sizeof(state_.subtitle));++state_.revision;}
 }else {
  const auto s=snapshot();const auto cmd=a.command;
  if(cmd==MediaCommand::Stop||cmd==MediaCommand::PowerToggle){stopRequested_=false;if(cancellation_)cancellation_->store(true);if(decoder_)audio_player_stop();{std::lock_guard<std::mutex> lock(mutex_);state_.state=MusicState::Idle;++state_.revision;}MediaSessionService::instance().releaseAudioFocus(mediaSourceId_);}
  else if(cmd==MediaCommand::PlayPause){if(decoder_&&s.state==MusicState::Playing)audio_player_pause();else if(decoder_&&s.state==MusicState::Paused)audio_player_resume();else if(currentUri_.length()){a.type=Action::Play;a.retainQueue=true;a.uri=currentUri_;a.title=s.title;execute(a);}else if(s.count){MusicTrack t;if(track(s.index,t)){a.type=Action::Play;a.index=s.index;a.uri=t.path;a.title=t.title;execute(a);}}}
  else if(cmd==MediaCommand::Next||cmd==MediaCommand::Previous){if(!queue_.empty()){const auto n=queue_.size();queueIndex_=s.shuffle&&n>1?(queueIndex_+1+esp_random()%(n-1))%n:(cmd==MediaCommand::Next?(queueIndex_+1)%n:(queueIndex_?queueIndex_-1:n-1));a.type=Action::Play;a.retainQueue=true;a.uri=queue_[queueIndex_];a.title=a.uri.substring(a.uri.lastIndexOf('/')+1);execute(a);}else if(s.count&&!currentUri_.startsWith("http")){size_t index=s.shuffle&&s.count>1?(s.index+1+esp_random()%(s.count-1))%s.count:(cmd==MediaCommand::Next?(s.index+1)%s.count:(s.index?s.index-1:s.count-1));MusicTrack t;if(track(index,t)){a.type=Action::Play;a.index=index;a.uri=t.path;a.title=t.title;execute(a);}}}
  else {a.type=Action::Volume;a.index=cmd==MediaCommand::MuteToggle?(s.volume?0:rememberedVolume_):std::max(0,std::min(100,int(s.volume)+(cmd==MediaCommand::VolumeUp?1:-1)));execute(a);}
 }
 publishMedia();
}
void MusicPlayerService::onAudio(unsigned event){
 bool next=false;{std::lock_guard<std::mutex> lock(mutex_);
  if(event==AUDIO_PLAYER_CALLBACK_EVENT_PLAYING||event==AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT)state_.state=MusicState::Playing;
  else if(event==AUDIO_PLAYER_CALLBACK_EVENT_PAUSE)state_.state=MusicState::Paused;
  else if(event==AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE){state_.state=MusicState::Error;strlcpy(state_.error,"Nur MP3 und PCM-WAV; kein AAC/HLS/DRM",sizeof(state_.error));}
  else if(event==AUDIO_PLAYER_CALLBACK_EVENT_IDLE&&state_.state!=MusicState::Error&&audio_player_get_state()==AUDIO_PLAYER_STATE_IDLE){next=state_.state==MusicState::Playing&&!currentUri_.startsWith("http")&&cancellation_&&!cancellation_->load();if(state_.state!=MusicState::Loading)state_.state=MusicState::Idle;}
  ++state_.revision;
 }
 if(next){const auto s=snapshot();if((s.queueCount?s.queueIndex+1<s.queueCount:s.index+1<s.count)||s.repeat)command(MediaCommand::Next);else MediaSessionService::instance().releaseAudioFocus(mediaSourceId_);}publishMedia();
}
void MusicPlayerService::publishMedia(){const auto s=snapshot();MediaState m;m.type=MediaSourceType::LocalFile;m.available=s.count>0||s.title[0];m.playing=s.state==MusicState::Playing;m.powered=s.state==MusicState::Playing||s.state==MusicState::Paused||s.state==MusicState::Loading;m.volume=s.volume;m.volumeMax=100;m.muted=!s.volume;m.title=s.title;m.subtitle=s.subtitle;m.source="Home-Display Musik";MediaSessionService::instance().update(mediaSourceId_,m);}
void MusicPlayerService::backgroundTick(){const unsigned generation=UsbStorageService::instance().generation();if(generation!=usbGeneration_){usbGeneration_=generation;libraryDirty_=true;}if(stopRequested_&&!busy_)command(MediaCommand::Stop);const unsigned events=audioEvents_.exchange(0);if(events){const auto native=audio_player_get_state();if(native==AUDIO_PLAYER_STATE_PLAYING)onAudio(AUDIO_PLAYER_CALLBACK_EVENT_PLAYING);else if(native==AUDIO_PLAYER_STATE_PAUSE)onAudio(AUDIO_PLAYER_CALLBACK_EVENT_PAUSE);else if(events&(1u<<AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE))onAudio(AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE);else if(events&(1u<<AUDIO_PLAYER_CALLBACK_EVENT_IDLE))onAudio(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);}if(libraryDirty_.exchange(false)&&!rescan())libraryDirty_=true;SystemSupervisor::instance().heartbeat(ServiceId::Media);}
}
