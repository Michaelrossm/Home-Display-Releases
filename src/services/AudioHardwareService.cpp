#include "AudioHardwareService.h"
#include "SettingsService.h"
#include "../core/SystemSupervisor.h"
#include "../core/EventLog.h"
#include "bsp/esp32_p4_platform.h"

namespace hp {

AudioHardwareService& AudioHardwareService::instance() {
    static AudioHardwareService a;
    return a;
}

bool AudioHardwareService::begin() {
    if (ready_) return true;

    volume_ = SettingsService::instance().snapshot().music.volume;
    codec_ = bsp_audio_codec_speaker_init();

    if (!codec_) {
        EventLog::instance().add("ERROR", "AUDIO_BSP", "Waveshare ES8311 init failed");
        return false;
    }

    ready_ = true;
    setVolume(volume_);
    EventLog::instance().add("INFO", "AUDIO_BSP", "Waveshare ES8311 ready");
    return true;
}

esp_err_t AudioHardwareService::applyFormat(
    uint32_t sampleRate,
    uint8_t bits,
    uint8_t channels
) {
    if (!ready_ || !codec_) return ESP_ERR_INVALID_STATE;
    if (sampleRate < 8000 || sampleRate > 192000) return ESP_ERR_INVALID_ARG;
    if (bits != 16 && bits != 24 && bits != 32) return ESP_ERR_INVALID_ARG;
    if (channels < 1 || channels > 2) return ESP_ERR_INVALID_ARG;

    if (opened_) {
        esp_codec_dev_close(codec_);
        opened_ = false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits,
        .channel = channels,
        .sample_rate = sampleRate,
    };

    if (esp_codec_dev_open(codec_, &fs) != ESP_CODEC_DEV_OK) {
        EventLog::instance().add("ERROR", "AUDIO_FORMAT", "esp_codec_dev_open failed");
        return ESP_FAIL;
    }

    opened_ = true;
    return ESP_OK;
}

size_t AudioHardwareService::writePcm(
    const void* data,
    size_t len,
    uint32_t timeoutMs
) {
    (void)timeoutMs;
    auto& self = instance();

    if (!self.ready_ || !self.opened_ || !self.codec_ || !data || !len) return 0;

    if (esp_codec_dev_write(self.codec_, const_cast<void*>(data), len) != ESP_CODEC_DEV_OK) {
        self.reportUnderrun();
        return 0;
    }

    SystemSupervisor::instance().heartbeat(ServiceId::Audio);
    return len;
}

esp_err_t AudioHardwareService::setFormat(
    uint32_t sampleRate,
    uint8_t bits,
    uint8_t channels
) {
    return instance().applyFormat(sampleRate, bits, channels);
}

bool AudioHardwareService::setVolume(uint8_t percent) {
    SystemSupervisor::instance().heartbeat(ServiceId::Audio);
    volume_ = min<uint8_t>(100, percent);

    if (!ready_ || !codec_) return false;
    return esp_codec_dev_set_out_vol(codec_, volume_) == ESP_CODEC_DEV_OK;
}

void AudioHardwareService::endMicrophone(){if(microphone_&&microphoneOpened_.exchange(false))esp_codec_dev_close(microphone_);}
bool AudioHardwareService::beginMicrophone(){
    if(microphoneOpened_)return true;
    if(!ready_)return false;
    // BSP owns the underlying GPIO/I2C/codec interfaces. Cache one lazy handle;
    // deleting just esp_codec_dev would leak those interfaces on every call.
    if(!microphone_){microphone_=bsp_audio_codec_microphone_init();if(!microphone_)return false;
        esp_codec_set_disable_when_closed(microphone_,false);}
    esp_codec_dev_sample_info_t format={.bits_per_sample=16,.channel=1,.sample_rate=16000};
    if(esp_codec_dev_open(microphone_,&format)!=ESP_CODEC_DEV_OK)return false;
    microphoneOpened_=true;return true;
}
size_t AudioHardwareService::readPcm(void* data,size_t bytes){
    // Short codec reads only from the shared worker. Never run this on LVGL.
    if(!microphoneOpened_||!data||!bytes||bytes>640||bytes%2)return 0;
    return esp_codec_dev_read(microphone_,data,bytes)==ESP_CODEC_DEV_OK?bytes:0;
}

}
