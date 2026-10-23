#pragma once
#include <Arduino.h>
#include <mutex>
namespace hp {
// Local, staged firmware installation. Web/UI requests only enqueue a transfer job.
class FirmwareMediaUpdateService final {
public:
 static FirmwareMediaUpdateService& instance();
 static constexpr const char* p4Path(){return "/Firmware/Firmware-P4.bin";}
 static constexpr const char* c6Path(){return "/Firmware/Firmware-C6.bin";}
 bool available() const;
 String storedInfo() const; // SD preferred, USB fallback. Worker/UI read-only.
 bool start(bool c6);
 bool busy() const;
 String status() const;
 // Immutable, short-locked copy for UI. Never call LVGL from the update worker.
 enum class Phase : uint8_t { Idle, Checking, Writing, Verifying, Ready, Failed, Rebooting };
 struct Progress {
  uint32_t generation=0;
  Phase phase=Phase::Idle;
  uint8_t percent=0;
  bool c6=false;
  String previousVersion, nextVersion, message;
 };
 Progress progress() const;
 // Direct HTTPS OTA uses the same LVGL overlay as SD/USB installation.
 // Caller must already own the OtaWriteSession lock.
 bool beginWebUpload(const String& previousVersion,const String& nextVersion);
 void webUploadProgress(Phase phase,uint8_t percent,const String& message);
 void endWebUpload(bool success,const String& message);
 void webRebooting();
 static bool validC6Size(size_t bytes){return bytes==1226976;}
 static bool validC6Hash(const uint8_t* digest);
private:
 static void worker(void*);
 void install();
 void setState(const String& msg,bool inProgress);
 void publish(Phase phase,uint8_t percent,const String& msg);
 mutable std::mutex mutex_;
 bool busy_=false, c6_=false;
 String status_="Lokaler Firmware-Updater bereit.";
 Progress progress_{};
};
}
