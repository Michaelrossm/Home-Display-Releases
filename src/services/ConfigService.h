#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "BuildInfo.h"

namespace hp {

enum class ConfigStatus : uint8_t { Uninitialized, Ready, Migrated, NewerSchema, Error };
enum class ConfigScope : uint8_t { Network, Ui, WebApps, CalendarAccounts, Media, UserSettings, AllUser };

class ConfigService {
public:
    static ConfigService& instance();
    bool begin();
    ConfigStatus status() const { return status_; }
    uint32_t schema() const { return schema_; }
    bool writable() const { return status_ == ConfigStatus::Ready || status_ == ConfigStatus::Migrated; }
    bool newerSchema() const { return status_ == ConfigStatus::NewerSchema; }
    uint32_t revision() const { return revision_.load(std::memory_order_acquire); }
    static bool validNvsKey(const char* key);

    String getString(const char* key, const char* fallback="") const;
    uint8_t getU8(const char* key, uint8_t fallback=0) const;
    uint16_t getU16(const char* key, uint16_t fallback=0) const;
    uint32_t getU32(const char* key, uint32_t fallback=0) const;
    bool getBool(const char* key, bool fallback=false) const;
    bool has(const char* key) const;

    bool putString(const char* key, const String& value);
    bool putU8(const char* key, uint8_t value);
    bool putU16(const char* key, uint16_t value);
    bool putU32(const char* key, uint32_t value);
    bool putBool(const char* key, bool value);
    bool remove(const char* key);
    enum class AppStateWriteResult { Ok, NotWritable, InvalidIdentity, InvalidRecord, WriteFailed };
    bool appEnabled(const char* id, bool fallback) const;
    AppStateWriteResult saveAppEnabled(const char* id, bool enabled);
    bool removeAppEnabled(const char* id);
    bool reset(ConfigScope scope);

    // Dedicated internal namespace for credentials/tokens; never exported by diagnostics.
    Preferences& secrets();
    Preferences& calendarSecrets();
    bool hasSecret(const char* key);
    bool credentialsReady();
    String getSecretString(const char* key, const char* fallback="");
    bool putSecretString(const char* key, const String& value);
    bool removeSecret(const char* key);

private:
    ConfigService() = default;
    bool migrate(uint32_t from, uint32_t to);
    bool migrateV1ToV2();
    bool clearKeys(const char* const* keys, size_t count);

    mutable Preferences prefs_;
    Preferences secrets_;
    Preferences calendarSecrets_;
    bool prefsOpen_ = false;
    bool secretsOpen_ = false;
    bool calendarSecretsOpen_ = false;
    uint32_t schema_ = 0;
    ConfigStatus status_ = ConfigStatus::Uninitialized;
    std::atomic<uint32_t> revision_{0};
    bool changed(bool ok){if(ok)revision_.fetch_add(1,std::memory_order_release);return ok;}
};
}
