#include "EmailAccountService.h"
#include "CredentialStore.h"
#include "SecurityService.h"
#include "CalendarSyncService.h"
#include "CalDavDiscovery.h"
#include <mbedtls/platform_util.h>
#include <cstring>

namespace hp {
namespace {
struct AccountRecord { uint8_t version, provider; char address[255]; char password[193]; };
bool valid(const AccountRecord& record) {
    return (record.version == 1 || record.version == 2) && (record.provider&127) < uint8_t(EmailProvider::Count) &&
        (record.version != 1 || record.provider < uint8_t(EmailProvider::Count)) &&
        memchr(record.address, 0, sizeof(record.address)) && memchr(record.password, 0, sizeof(record.password));
}
bool normalize(const String& input, String& output) {
    output = input; output.trim();
    if (output.length() < 3 || output.length() > 254) return false;
    const int at = output.indexOf('@');
    if (at <= 0 || at != output.lastIndexOf('@') || at + 1 >= int(output.length())) return false;
    const String domain = output.substring(at + 1);
    if (domain.indexOf('.') <= 0 || domain.endsWith(".")) return false;
    for (size_t i = 0; i < output.length(); ++i) {
        const auto c = uint8_t(output[i]);
        if (c <= 32 || c >= 127 || c == '<' || c == '>' || c == '\\' || c == '"') return false;
    }
    // Preserve the local-part; domain matching is case-insensitive.
    String lower = domain; lower.toLowerCase(); output = output.substring(0, at + 1) + lower;
    return true;
}
}
EmailAccountService& EmailAccountService::instance() { static EmailAccountService service; return service; }
EmailAccountInfo EmailAccountService::account(uint8_t slot) {
    EmailAccountInfo info;
    AccountRecord record{};
    if (slot < kMaxAccounts && CredentialStore::instance().readEmailRecord(slot, &record, sizeof(record)) && valid(record)) {
        info.address = record.address; info.provider = EmailProvider(record.provider&127); info.configured = true;
        info.oauthManaged = record.version==2&&(record.provider&128);
    }
    mbedtls_platform_zeroize(&record, sizeof(record));
    return info;
}
bool EmailAccountService::save(uint8_t slot, EmailProvider provider, const String& input, const char* password) {
    SecurityWriteGuard guard;
    if (slot >= kMaxAccounts || provider >= EmailProvider::Count ||
        !SecurityService::instance().settingsAllowed() || SecurityService::instance().tabletLocked()) return false;
    String address; if (!normalize(input, address)) return false;
    // Changing a shared CalDAV slot requires explicit removal first.
    if(CalendarSyncService::instance().calDavServer(slot).length())return false;
    const auto previous=account(slot);
    const bool changed=previous.configured&&(previous.address!=address||previous.provider!=provider);
    AccountRecord record{};
    bool ok = false;
    const size_t size = password ? strnlen(password, sizeof(record.password)) : 0;
    if (size >= sizeof(record.password)) return false;
    if (!size) {
        // Empty field on edit keeps the secret only for the same identity.
        const bool loaded = CredentialStore::instance().readEmailRecord(slot, &record, sizeof(record));
        if (!loaded || !valid(record) || address != record.address || uint8_t(provider) != (record.provider&127)) {
            mbedtls_platform_zeroize(&record, sizeof(record));
            if(provider!=EmailProvider::Google&&provider!=EmailProvider::Microsoft)return false;
        }
    } else {
        for (size_t i = 0; i < size; ++i) if (uint8_t(password[i]) < 32 || uint8_t(password[i]) == 127) return false;
        memcpy(record.password, password, size);
    }
    record.version = 1; record.provider = uint8_t(provider);
    if(provider==EmailProvider::Apple&&!CalDavDiscovery::appleAppPassword(record.password)){mbedtls_platform_zeroize(&record,sizeof(record));return false;}
    memcpy(record.address, address.c_str(), address.length() + 1);
    if(changed){
        if(!CalendarSyncService::instance().disconnect(slot)){mbedtls_platform_zeroize(&record,sizeof(record));return false;}
        const String id=String("oauth_")+slot;
        if(!CredentialStore::instance().removeOAuthRecord(id.c_str())){mbedtls_platform_zeroize(&record,sizeof(record));return false;}
    }
    ok = CredentialStore::instance().writeEmailRecord(slot, &record, sizeof(record));
    mbedtls_platform_zeroize(&record, sizeof(record));
    if(ok)CalendarSyncService::instance().accountChanged(slot);
    return ok;
}
bool EmailAccountService::storeOAuthIdentity(uint8_t slot,EmailProvider provider,const String& input){
    SecurityWriteGuard guard;
    if(slot>=kMaxAccounts||(provider!=EmailProvider::Google&&provider!=EmailProvider::Microsoft)||
        !SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    String address;if(!normalize(input,address))return false;
    const auto previous=account(slot);
    if(previous.configured&&(!previous.oauthManaged||previous.provider!=provider)){
        String before=previous.address,after=address;before.toLowerCase();after.toLowerCase();
        return previous.provider==provider&&before==after; // Never overwrite an explicit account binding.
    }
    AccountRecord record{};record.version=2;record.provider=uint8_t(provider)|128;
    memcpy(record.address,address.c_str(),address.length()+1); // No password is captured or stored.
    const bool ok=CredentialStore::instance().writeEmailRecord(slot,&record,sizeof(record));
    mbedtls_platform_zeroize(&record,sizeof(record));return ok;
}
bool EmailAccountService::remove(uint8_t slot) {
    SecurityWriteGuard guard;
    if(slot>=kMaxAccounts||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    if(!CalendarSyncService::instance().disconnect(slot))return false;
    const String id=String("oauth_")+slot;
    return CredentialStore::instance().removeOAuthRecord(id.c_str())&&CredentialStore::instance().removeEmailRecord(slot);
}
bool EmailAccountService::loadCredentials(uint8_t slot, char* address, size_t addressSize,
                                         char* password, size_t passwordSize, EmailProvider& provider) {
    if (!address || !password) return false;
    if (addressSize) mbedtls_platform_zeroize(address, addressSize);
    if (passwordSize) mbedtls_platform_zeroize(password, passwordSize);
    AccountRecord record{};
    const bool ok = slot < kMaxAccounts && CredentialStore::instance().readEmailRecord(slot, &record, sizeof(record)) &&
        valid(record) && strlen(record.address) < addressSize && strlen(record.password) < passwordSize;
    if (ok) { strcpy(address, record.address); strcpy(password, record.password); provider = EmailProvider(record.provider&127); }
    mbedtls_platform_zeroize(&record, sizeof(record)); return ok;
}
const char* EmailAccountService::providerName(EmailProvider provider) {
    const char* names[] = {"Google / Gmail", "Microsoft / Outlook", "Apple / iCloud", "GMX", "WEB.DE", "Yahoo", "Telekom / T-Online", "Anderer Anbieter"};
    return provider < EmailProvider::Count ? names[uint8_t(provider)] : "Unbekannt";
}
const char* EmailAccountService::providerHint(EmailProvider provider) {
    switch (provider) {
        case EmailProvider::Google: return "Google: Anmeldung nur auf der offiziellen Anbieterseite. Gerätecode für Profil; Kalender benötigt Web-OAuth mit HTTPS-Rückleitung. Adresse wird automatisch übernommen.";
        case EmailProvider::Microsoft: return "Outlook: öffentlicher OAuth-Client, Anmeldung per Gerätecode auf der offiziellen Microsoft-Seite. Keine Anbieter-Passwörter hier eingeben.";
        case EmailProvider::Apple: return "iCloud: anwendungsspezifisches Passwort verwenden, nicht das normale Apple-Account-Passwort.";
        case EmailProvider::Gmx: case EmailProvider::WebDe: return "Bei Zwei-Faktor-Schutz App-Passwort verwenden. Mail-/Kalenderzugriff muss beim Anbieter freigegeben sein.";
        case EmailProvider::Yahoo: return "Für externe Anwendungen ggf. ein eigenes App-Passwort erzeugen.";
        case EmailProvider::Telekom: return "T-Online: separates Passwort für E-Mail-Programme verwenden.";
        default: return "Zugangsdaten werden gespeichert. Unterstützte Protokolle und Autorisierung ergänzt der spätere Sync-Dienst.";
    }
}
}
