#pragma once
#include <Arduino.h>
#include <esp_err.h>
#include <esp_codec_dev.h>
#include <atomic>

namespace hp {

class AudioHardwareService {
public:
    static AudioHardwareService& instance();

    bool begin();
    bool ready() const { return ready_; }
    bool beginMicrophone();
    void endMicrophone();
    bool microphoneReady()const{return microphoneOpened_.load();}
    size_t readPcm(void* data,size_t bytes);

    static size_t writePcm(const void* data, size_t len, uint32_t timeoutMs);
    static esp_err_t setFormat(uint32_t sampleRate, uint8_t bits, uint8_t channels);

    bool setVolume(uint8_t percent);
    void reportUnderrun() { ++underruns_; }
    uint32_t underruns() const { return underruns_; }

private:
    esp_err_t applyFormat(uint32_t sampleRate, uint8_t bits, uint8_t channels);

    bool ready_ = false;
    bool opened_ = false;
    uint8_t volume_ = 55;
    uint32_t underruns_ = 0;
    esp_codec_dev_handle_t codec_ = nullptr;
    esp_codec_dev_handle_t microphone_ = nullptr;
    std::atomic<bool> microphoneOpened_{false};
};

}
