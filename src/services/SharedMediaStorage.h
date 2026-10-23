#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <atomic>
namespace hp {
class SharedMediaStorage final {
public:
 static SharedMediaStorage& instance();
 static const char* const* folders();
 static size_t folderCount();
 // auto: SD first, then USB. sd/usb: prefer selected media but fall back if missing.
 String preference() const;
 bool setPreference(const String& preference);
 String primaryUri() const;
 bool prepareSdFolders() const;
 bool prepareUsbFolders() const;
private:
 mutable std::atomic<uint64_t> cachedPreference_{0};
};
}
