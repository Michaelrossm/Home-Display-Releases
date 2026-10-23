# Central cloud account interface

`CloudAccountService` is the public name of the existing `CalendarSyncService`
singleton, not another runtime. Its five slots are shared with the existing
native account registry. OAuth remains central. iCloud and generic CalDAV use
one `CalDavConnector`, with namespace-aware discovery in the existing CalDavCore.
No calendar/mail app, per-account task or additional web server was added.

## App API

```cpp
#include "services/CloudAccountService.h"

auto& accounts = hp::CloudAccountService::instance();
auto calendars = accounts.accountsWithCapability(hp::AccountCapability::Calendar);
for (const auto& account : calendars) {
    auto client = accounts.calendarClient(account.id);
    auto collections = client.collections();
    // Returns a request acceptance result; never blocks on provider HTTP.
    client.requestRefresh();
}
```

Other APIs: `accounts()`, `accountInfo(id)`, `isConnected(id)`, `capabilities(id)`,
`requestCapability(id, capability)`, `authorizeCapability(id, capability)`,
`setAccountEnabled(id, enabled)`, `disconnectAccount(id)`, `removeAccount(id)`.
IDs currently use the fixed shared slot format `account-0` through `account-4`;
apps must re-query after account changes and must not assume a deleted slot keeps
the same identity. Client values contain only the ID, never a token or password.

`requestCapability` checks currently granted capabilities. A missing grant returns
`PermissionMissing` and publishes the corresponding event; it does not silently
start an interactive login. After explicit user approval, `authorizeCapability`
queues the existing provider flow for that precise slot. Google calendar uses
Web OAuth/HTTPS; Google profile uses the limited-input device flow. Microsoft
uses the existing public-client device flow without a client secret. Incremental
calendar authorization retains the central scope definitions and identity binding.

## Implemented versus future capabilities

- Microsoft/Google: Profile; Calendar after the calendar-specific authorization.
- iCloud/CalDAV: Calendar after authenticated discovery and a successful connection
  test. Collections are available through the same app-facing client.
- Contacts: opt-in per account (off by default). Google People / Microsoft Graph
  require explicit read authorization; iCloud / custom servers use CardDAV and
  the centrally stored app password. `PhonebookService` exposes worker-only local
  records plus a separate read-only provider snapshot; see [details](appointment-details.md).
- Mail and Files remain future capabilities and are **not** reported as granted.
- Existing Microsoft/Google primary-calendar snapshots remain available through
  `CloudCalendarClient::copyEvents`. CalDAV currently provides discovery and
  collection refresh, **not REPORT/event import, writes or recurrence expansion**.
  Its event snapshot is empty. Apps must not confuse discovery with event sync.

No arbitrary Microsoft Graph/Google HTTP proxy or token getter is exposed to apps.
Future operations should be implemented in the central connectors, not app code.

## iCloud and CalDAV setup

Use Settings → Email accounts on the display. Select iCloud or own CalDAV server,
enter the username and protected app-password field; for generic CalDAV enter an
HTTPS URL including its optional port/base path. Save and choose connection test.
For iCloud, only the app-specific `xxxx-xxxx-xxxx-xxxx` format is accepted; the
normal Apple account password is not requested. The format cannot itself prove
Apple issued it: the authenticated server test verifies the credential.
[Apple app-specific password instructions](https://support.apple.com/en-gb/102654).

The existing HTTP wizard at `/accounts` guides setup, accepts public server/user
metadata and starts asynchronous tests. It deliberately accepts **no passwords**
over HTTP. Password provisioning is exclusively on the PIN-protected native form.
No insecure trusted-proxy header or fake HTTPS detection was added.

Discovery checks `current-user-principal`, `calendar-home-set` and calendar
resource types in successful DAV propstats. Well-known is a fallback when the
configured path returns 404. Up to eight home sets and sixteen calendars are
handled. Entity expansion/DTDs, excessive nesting/body size and invalid XML are
rejected. Generic redirects/hrefs must remain at the configured HTTPS origin;
iCloud may use certificate-validated hosts under `.icloud.com`. No passwords are
forwarded to arbitrary third-party hosts or downgraded HTTP URLs.
[CalDAV discovery](https://www.rfc-editor.org/rfc/rfc4791.html),
[well-known service discovery](https://www.rfc-editor.org/rfc/rfc6764.html).

TLS uses the existing certificate bundle. Self-signed/custom-CA certificates,
IPv6 URL literals, DNS SRV discovery, cross-origin generic redirects and Digest
authentication are not implemented; certificate verification is never disabled.

## Persistence, state and events

Public metadata lives separately under Config `/accounts/`. Credentials remain
in encrypted CredentialStore records under Config `/credentials/`, bound to slot,
provider, server and username. Atomic recovery validates metadata; invalid enabled
metadata fails closed. Persisted capability metadata is descriptive only: grants
are reconstructed from the bound central token record or live CalDAV test.
Access tokens remain private volatile caches as before; refresh tokens persist
encrypted. Secrets are never part of AccountInfo or JSON status exports.

Disable preserves credentials but stops that account's scheduled network work
and removes it from usable capability queries. Re-enable restores centrally.
Disconnect invalidates queued generations, wipes private access caches and deletes
local token/password records and calendar caches. Failed deletion retries instead
of restoring an old grant. Public metadata remains so the account can be reconnected.
Remove additionally deletes metadata and the shared native record. Provider-side
OAuth grants must still be revoked in the provider's security settings if desired.

States: Disconnected, Connecting, AwaitingUser, Connected, NeedsLogin, TokenExpired,
InvalidCredentials, PermissionMissing, ServerUnavailable, TlsError, Error.
EventBus events: AccountConnected, AccountDisconnected, AccountAuthExpired,
AccountSyncError, AccountCapabilitiesChanged, AccountPermissionMissing.
Publication happens on the existing main/background tick after releasing account
locks, never inside HTTP/TLS callbacks. Payload is a synchronous `AccountInfo*`;
subscribers must copy it if retained and use their UI queue for visual changes.

All network/discovery/configuration jobs reuse NetworkWorker. Provider status
endpoints read cached data. No live provider/server test has been performed during
implementation; registrations, credentials and end-to-end hardware testing remain
necessary. Existing Microsoft/Google client-ID and Google HTTPS requirements are
documented in [oauth-calendar.md](oauth-calendar.md).
