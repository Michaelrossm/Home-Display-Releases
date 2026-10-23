#pragma once
#include <Arduino.h>
namespace hp {
class CredentialStore {
public:
    static CredentialStore& instance();
    bool putNasPassword(uint32_t id, const String& password);
    String nasPassword(uint32_t id);
    bool removeNasPassword(uint32_t id);
private:
    friend class WebTlsIdentity;
    friend class WebLoginPersistence;
    friend class EmailAccountService;
    friend class CalendarSyncService;
    bool writeOAuthRecord(const char* id,const void*,size_t);
    bool readOAuthRecord(const char* id,void*,size_t);
    bool removeOAuthRecord(const char* id);
    bool writeEmailRecord(uint8_t slot, const void*, size_t);
    bool readEmailRecord(uint8_t slot, void*, size_t);
    bool removeEmailRecord(uint8_t slot);
    bool emailKey(uint8_t out[32], bool create);
    String key(uint32_t id) const;
};
}
