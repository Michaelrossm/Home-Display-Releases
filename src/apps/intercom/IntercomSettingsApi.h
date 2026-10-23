#pragma once
#include "../../services/intercom/IntercomProvider.h"
namespace hp {
// Trusted native/web callers only; writes/start run on the existing worker.
size_t nativeIntercomDevices(IntercomDevice*,size_t);
bool saveNativeIntercomDevice(const IntercomDevice&,const String* password,String& error);
bool removeNativeIntercomDevice(const char* id,String& error);
bool startNativeIntercomVideo(const IntercomDevice&,String& error);
constexpr const char* legacyIntercomId="legacy-goliath";
}
