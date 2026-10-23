# Central email accounts

Settings manages five independent slots through `EmailAccountService`.
Providers: Gmail, Outlook, iCloud, GMX, WEB.DE, Yahoo, Telekom, and other.
Saving a slot is **not** an authentication test or an active mail/calendar sync.
Google/Microsoft OAuth and read-only calendar synchronization live in the central
`CalendarSyncService`. See [OAuth setup](oauth-calendar.md) for required provider
registrations and HTTPS deployment. Password-only storage never authenticates OAuth.

## App integration

Native apps can request `account(slot)` metadata: address, provider, configured.
No public password getter, HTTP endpoint, diagnostic export, or Lua bridge exists.
Central `CalendarSyncService`/future `EmailSyncService` may use the private
credential loader; app code should only request sync through these services.
Their implementation must validate TLS certificates and enforce account permissions.
Credentials must never be passed back to app code, logged, or attached to events.

## Persistence and boundaries

- One authenticated AES-256-GCM record per slot in the central credential store.
- Email, provider, and password are stored together, preventing partial profile updates.
- Random device-local 256-bit key and fresh 96-bit nonce per write.
- Version and slot are authenticated, so ciphertext cannot be swapped between slots.
- Corrupt records or a missing key fail closed; existing records never cause key regeneration.
- Empty password preserves a secret only for an unchanged address/provider.
  Google/Microsoft may also be saved without a password for OAuth-only use.
- Settings PIN/tablet locks are checked under the existing security write guard.
- The form never preloads a saved password and wipes its password buffer on save,
  slot change, and destruction. Last-character disclosure is disabled.

The local key currently lives in the same NVS credential namespace as the encrypted
records. This protects against accidental plaintext display/export, **not** a complete
physical flash dump or compromised firmware. Flash encryption, secure boot and
eFuse provisioning require a separate reviewed hardware-security workflow.
NVS deletion is logical deletion, not guaranteed forensic erasure.

## Provider requirements

- [Google app passwords](https://support.google.com/mail/answer/185833)
- [Google Calendar OAuth](https://developers.google.com/workspace/calendar/api/auth)
- [Outlook OAuth requirement](https://support.microsoft.com/en-us/outlook/pop-imap-and-smtp-settings-for-outlook-com)
- [Apple app-specific passwords](https://support.apple.com/en-ie/102654)
- [GMX app passwords](https://hilfe.gmx.net/account/logindaten/app-passwort.html)
- [WEB.DE app passwords](https://hilfe.web.de/account/logindaten/app-passwort.html)
- [Yahoo app passwords](https://ca.help.yahoo.com/kb/account/generate-manage-rd-party-passwords-sln15241.html)
- [Telekom email-program password](https://www.telekom.de/hilfe/apps-dienste/e-mail/programm-passwort-verwalten)

Password-only account storage must not be advertised as a working OAuth login.
