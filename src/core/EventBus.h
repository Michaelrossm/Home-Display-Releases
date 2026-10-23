#pragma once
#include <Arduino.h>
namespace hp {
enum class EventType : uint8_t {
  SystemReady, NetworkUp, NetworkDown, SdInserted, SdRemoved, TimeSynced,
  AppOpened, MediaChanged, MediaCommand, MediaLibraryChanged, IdleStateChanged, PowerStateChanged, CalendarChanged, CalendarSyncChanged, OtaAvailable,
  OtaStarted, OtaFinished, DoorbellRing, VoiceCommand,
  AccountConnected,AccountDisconnected,AccountAuthExpired,AccountSyncError,
  AccountCapabilitiesChanged,AccountPermissionMissing,ThemeChanged,StorageProviderChanged
};
struct Event { EventType type; const void* payload = nullptr; };
struct StorageProviderEvent {uint32_t id;bool online;};
using EventHandler = void(*)(const Event&);
class EventBus {
 public: static EventBus& instance(); bool subscribe(EventHandler h); void publish(const Event& e);
 private: static constexpr size_t MAX=16; EventHandler h_[MAX]{}; size_t n_=0;
};
}
