#pragma once
#include "MusicTypes.h"
#include "MusicLibrary.h"
#include "../../core/BackgroundScheduler.h"
#include "../../core/EventBus.h"
#include "../../services/MediaSessionService.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
namespace hp {
class MusicPlayerService:public IBackgroundJob {
public:
 static MusicPlayerService& instance();bool begin();void backgroundTick()override;
 BackgroundJobClass backgroundClass()const override{return BackgroundJobClass::Quick;}
 uint32_t backgroundIntervalMs()const override{return 250;}const char* backgroundName()const override{return "music";}
 MusicSnapshot snapshot();bool track(size_t,MusicTrack&);bool playIndex(size_t);bool rescan();void setVolume(uint8_t);bool command(MediaCommand);
 bool playUri(const String&,const String& title="Internetradio");
 bool playQueue(const std::vector<String>&,size_t index);
 String preferences();bool saveOptions(const String&);bool favorite(const String&);bool toggleFavorite(const String&);
 String playingUri(){std::lock_guard<std::mutex> lock(mutex_);return currentUri_;}
private:
 static bool mediaCommandThunk(MediaCommand);static void eventThunk(const Event&);
 void publishMedia();void error(const char*);bool ensureDecoder();
 struct Action;static void actionWorker(void*);bool enqueue(Action*);
 void execute(Action&);void onAudio(unsigned);
 std::mutex mutex_;MusicSnapshot state_{};std::shared_ptr<MusicLibrary> library_;
 std::shared_ptr<std::atomic<bool>> cancellation_;String preferences_="{}",currentUri_;
 std::atomic<bool> busy_{false},libraryDirty_{false},stopRequested_{false};std::atomic<unsigned> audioEvents_{0};
 uint32_t mediaSourceId_=0;bool decoder_=false;uint8_t rememberedVolume_=55;
 std::vector<String> queue_;size_t queueIndex_=0;
 uint32_t usbGeneration_=0;
};
}
