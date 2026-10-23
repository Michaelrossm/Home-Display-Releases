#pragma once
#include <Arduino.h>

namespace hp {
enum class EmailProvider : uint8_t { Google, Microsoft, Apple, Gmx, WebDe, Yahoo, Telekom, Other, Count };
struct EmailAccountInfo {
    String address;
    EmailProvider provider = EmailProvider::Google;
    bool configured = false;
    bool oauthManaged = false;
};
// Shared native account registry. Apps get metadata, never passwords.
// Provider authentication/synchronization lives in CalendarSyncService.
class EmailAccountService {
public:
    static constexpr uint8_t kMaxAccounts = 5;
    static EmailAccountService& instance();
    EmailAccountInfo account(uint8_t slot);
    bool save(uint8_t slot, EmailProvider provider, const String& address, const char* password);
    bool remove(uint8_t slot);
    static const char* providerName(EmailProvider);
    static const char* providerHint(EmailProvider);
private:
    // Explicitly limited to central native sync services; no Lua/HTTP/UI getter.
    friend class CalendarSyncService;
    bool storeOAuthIdentity(uint8_t slot,EmailProvider provider,const String& address);
    friend class EmailSyncService;
    bool loadCredentials(uint8_t slot, char* address, size_t addressSize,
                         char* password, size_t passwordSize, EmailProvider& provider);
};
}
