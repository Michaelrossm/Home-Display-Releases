#include "IntercomAudioService.h"
#include "AudioHardwareService.h"
#include "MediaSessionService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include "IntercomService.h"
#include "../core/AppRegistry.h"
#include "intercom/MediaProtocol.h"
namespace hp {
    IntercomAudioService &IntercomAudioService::instance() {
        static IntercomAudioService s;
        return s;
    }
    bool IntercomAudioService::begin() {
        if (!source_)source_ = MediaSessionService::instance().registerSource(
            MediaSourceType::Intercom, "Sprechanlage", [](MediaCommand c) {
                if (c != MediaCommand::Stop)
                    return false;
                if (!NetworkWorker::instance().isWorkerContext()) {
                    NetworkWorker::instance().submitPriority(
                        [](void *) { IntercomAudioService::instance().stop(); }, nullptr,
                        "intercom-focus-stop");
                    return false;
                }
                IntercomAudioService::instance().stop();
                return true;
            });
        if(source_&&!maintenanceRegistered_)maintenanceRegistered_=NetworkWorker::instance().addMaintenance(maintenance,this);
        return source_ != 0&&maintenanceRegistered_;
    }
    bool IntercomAudioService::start(bool microphone) {
        return startAudio(microphone,false);
    }
    bool IntercomAudioService::startAudio(bool microphone,bool ringing) {
        if (!NetworkWorker::instance().isWorkerContext() ||
            (!ringing&&SecurityService::instance().tabletLocked()))
            return false;
        if(active()){
            // Preserve the interrupted source/focus while accepting a ringing call.
            if(!ringing_||ringing)return false;
            if(microphone&&!AudioHardwareService::instance().beginMicrophone())return false;
            microphoneActive_=microphone;ringing_=false;return true;
        }
        auto &media = MediaSessionService::instance();
        MediaState before;
        if (!media.trySnapshot(before) || !begin())
            return false;
        previous_ = before.sourceId;
        resume_ = before.playing;
        if (!media.requestAudioFocus(source_, true))
            return false;
        if (AudioHardwareService::setFormat(16000, 16, 1) != ESP_OK ||
            (microphone && !AudioHardwareService::instance().beginMicrophone())) {
            stop();
            return false;
        }
        active_.store(true, std::memory_order_release);
        microphoneActive_=microphone;
        ringing_=ringing;
        muted_ = false;
        MediaState state;
        state.type = MediaSourceType::Intercom;
        state.available = true;
        state.playing = true;
        state.powered = true;
        state.title = ringing?"Klingeln":"Gespraech";
        media.update(source_, state);
        return true;
    }
    void IntercomAudioService::stop() {
        if (!NetworkWorker::instance().isWorkerContext())
            return;
        auto &media = MediaSessionService::instance();
        const bool owned = media.focusedSource() == source_;
        const bool restore = owned && media.focusState() != AudioFocusState::Switching;
        active_ = false;
        microphoneActive_=false;
        ringing_=false;
        muted_ = false;
        AudioHardwareService::instance().endMicrophone();
        if (owned)
            media.releaseAudioFocus(source_);
        // A newer user-selected source or a PIN lock always wins over restoration.
        if (restore && previous_ && resume_ && !media.focusedSource() &&
            !SecurityService::instance().tabletLocked())
            media.commandSource(previous_, MediaCommand::PlayPause);
        previous_ = 0;
        resume_ = false;
    }
    void IntercomAudioService::maintenance(void* context){static_cast<IntercomAudioService*>(context)->pollRing();}
    void IntercomAudioService::pollRing(){
        if(!NetworkWorker::instance().isWorkerContext())return;
        IntercomSnapshot snapshot;
        if(!IntercomService::instance().snapshot(snapshot))return;
        if(snapshot.state!=IntercomState::Ringing){if(ringing_)stop();return;}
        IntercomDevice device;AppRegistryEntry owner;
        if(!IntercomService::instance().device(snapshot.device,device)||
            !AppRegistry::instance().lookup(device.owner,owner)||owner.state!=AppRegistryState::Enabled||
            !(owner.permissions&perm(AppPermission::Audio))){if(ringing_)stop();return;}
        if(!device.ringTone){if(ringing_)stop();return;}
        if(ringCall_!=snapshot.call){ringCall_=snapshot.call;ringNext_=0;}
        if(int32_t(millis()-ringNext_)<0||NetworkWorker::instance().priorityDepth())return;
        ringNext_=millis()+1500;
        if(!active()&&!startAudio(false,true))return;
        if(!ringing_)return;
        // Short bounded notification through the existing PCM path. Never open RX.
        static constexpr int16_t tone[20]={0,1236,2351,3236,3804,4000,3804,3236,2351,1236,0,-1236,-2351,-3236,-3804,-4000,-3804,-3236,-2351,-1236};
        for(unsigned i=0;i<320;++i)pcm_[i]=tone[i%20];
        if(AudioHardwareService::writePcm(pcm_,640,20)!=640)
            IntercomService::instance().diagnostic("ring_audio_unavailable");
    }
    bool IntercomAudioService::receiveG711(const uint8_t *bytes, size_t count, bool alaw) {
        if (!NetworkWorker::instance().isWorkerContext() || !active() || ringing_ || !bytes || count > 320 ||
            MediaSessionService::instance().focusedSource() != source_)
            return false;
        for (size_t i = 0; i < count; ++i) {
            const auto v = alaw ? intercom::alawDecode(bytes[i]) : intercom::mulawDecode(bytes[i]);
            pcm_[i * 2] = v;
            pcm_[i * 2 + 1] = v;
        }
        return AudioHardwareService::writePcm(pcm_, count * 4, 40) == count * 4;
    }
    size_t IntercomAudioService::captureG711(uint8_t *out, size_t count, bool alaw) {
        if (!NetworkWorker::instance().isWorkerContext() || !active() || !microphoneActive_ || muted_ || !out ||
            count > 160)
            return 0;
        if (AudioHardwareService::instance().readPcm(pcm_, count * 4) != count * 4)
            return 0;
        for (size_t i = 0; i < count; ++i) {
            const int16_t sample = (int32_t(pcm_[i * 2]) + pcm_[i * 2 + 1]) / 2;
            out[i] = alaw ? intercom::alawEncode(sample) : intercom::mulawEncode(sample);
        }
        return count;
    }
} // namespace hp
