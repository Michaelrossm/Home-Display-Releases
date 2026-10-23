# OAuth and calendar integration

This is configurable firmware infrastructure, not pre-registered Google/Microsoft
credentials. Actual provider login requires your own registration and hardware
validation. No live login was performed during implementation.

## Guided local account wizard

The wizard now also supports the central provider-neutral account interface,
iCloud and generic CalDAV. See [cloud-accounts.md](cloud-accounts.md) for the app
API, capability semantics, protected native password setup and limitations.

Open `http://<display-ip>:8088/accounts` using the existing web-admin credentials.
Unlock settings on the display. Select Microsoft or Google, open the official
provider link and enter the displayed user code there. No provider password is
entered in the wizard. Email/name are retrieved after provider authorization;
an empty account slot is created automatically, within the existing five-slot
registry. Explicitly saved account identities remain bound and cannot silently
be overwritten by a different login.

The central service exposes Idle, RequestingCode, AwaitingUser, Polling, Connected,
Expired, Cancelled and Error. Provider requests and polling use the existing
NetworkWorker; HTTP status responses read cached metadata only. Cancel invalidates
queued generations without deleting a previous successful grant. Disconnect
invalidates pending work and retries failed local token/cache deletion.

Standard mode hides client IDs. `include/OAuthClientDefaults.h` provides optional
public project client IDs; all defaults are intentionally empty because no genuine
project registrations were supplied. Never substitute another application's ID.
Without a default, the wizard explains the missing setup. Experts can select
their own public client ID and, for Google calendar, its HTTPS redirect. These
settings use existing config/encrypted credential services. Secrets are never
accepted by the HTTP configuration API or compiled into firmware.

Microsoft's wizard login requests only `openid profile offline_access User.Read`
using its public-client device flow. Calendar authorization remains a separate
native/calendar action with delegated calendar-read permission.

Google profile login uses a **TVs and Limited Input devices** OAuth registration,
with `openid email profile`. Google also requires this device client's secret
at the token endpoint: enter it only in the protected masked native display form
with the profile/device purpose selected. See the
[official Google device-flow documentation](https://developers.google.com/identity/protocols/oauth2/limited-input-device).
Calendar scopes are not supported by that flow. The separate “Kalender freigeben”
action uses the existing Web flow with HTTPS redirect, state and S256 PKCE.

Protected APIs: `GET /api/accounts`, `GET /api/accounts/login/status`, provider
`POST /api/accounts/{microsoft,google}/start` and `/disconnect`,
`POST /api/accounts/login/cancel`, `GET/POST /api/accounts/config`.
POSTs require admin authentication, JSON and the portal's anti-CSRF header.
Status exports only provider/state, public instructions/code/expiry, metadata
and connection flags. No tokens, secrets or private device codes are exported.
Legacy `/accounts/action` remains available for existing native/calendar clients.

## Optional native calendar setup

1. Settings → Email accounts: save one of five Google/Microsoft accounts. Its
   password can remain empty. OAuth verifies that the signed-in provider identity
   matches the saved address; aliases not returned by the provider are rejected.
2. Configure the provider's **client ID** in the OAuth card. The configuration is
   shared by that provider's slots, not duplicated per app.
3. Google additionally needs its Web application's client secret and exact
   registered HTTPS redirect ending in `/oauth/callback`. Enter the secret **only
   on the masked native display form**. Empty retains an existing secret for the
   same client ID. Microsoft uses a public client and needs no client secret.
4. Select the Calendar purpose on the native form and choose Connect, then open
   the wizard's provider link. Microsoft displays its short user code.
   Keep the display online, with a valid clock. Settings must be unlocked for
   connect/disconnect/configuration; web authentication does not bypass the PIN.

Saving an account is not a connection test. The page separately reports login,
identity verification, and calendar synchronization outcomes.

## Google registration and HTTPS callback

- Enable Google Calendar API. Create an OAuth **Web application**, configure the
  consent screen and permitted test users as appropriate.
- Register the exact URI, for example `https://auth.example.com/oauth/callback`.
  Google Web OAuth requires registered redirect URIs and application credentials:
  [Google Web OAuth documentation](https://developers.google.com/identity/protocols/oauth2/web-server).
- Firmware sends state, S256 PKCE and offline consent, using only `openid email`
  plus `https://www.googleapis.com/auth/calendar.events.readonly`.
- Deploy an HTTPS reverse proxy with a valid certificate under your control.
  Forward **only** `/oauth/callback` to the P4 port 8088; deny other external paths.
  Keep the upstream on an isolated trusted network or use a protected tunnel.
  Disable query/access logging on the callback (authorization codes are secrets).
  No domain, proxy, tunnel, port forwarding or cloud application is auto-created.
- The callback cannot require Basic auth: it uses a 256-bit one-use state, a
  10-minute session, account binding and PKCE. Replays/expired sessions are refused.
- Google limited-input device authorization is not used for calendar scopes:
  [supported device-flow scopes](https://developers.google.com/identity/protocols/oauth2/limited-input-device#allowedscopes).

Example Nginx location inside an **already configured HTTPS virtual host**:

```nginx
location = /oauth/callback {
    access_log off;
    proxy_pass http://DISPLAY_LAN_IP:8088;
    proxy_set_header Host $host;
    proxy_set_header Referrer-Policy no-referrer;
}
location / { return 404; }
```

Replace the placeholder IP; TLS certificates, DNS, routing and upstream isolation
remain deployment prerequisites. Do not forward all of port 8088 to the Internet.

## Microsoft registration

Register an app supporting the account types needed (including personal Outlook
accounts if required). Enable public client/device-code flows. Grant delegated
`User.Read` and `Calendars.Read`; organizational policy may require admin
consent. Firmware requests `openid profile offline_access User.Read Calendars.Read`
against the `common` tenant. It follows provider polling intervals and `slow_down`,
never exposing the private device code to the browser or apps.

[Microsoft device flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code)
and [calendarView permissions](https://learn.microsoft.com/en-us/graph/api/user-list-calendarview?view=graph-rest-1.0).

## Synchronization and app access

- Read-only standard/primary calendar, previous day through next 30 days.
- Maximum 32 events per account; the UI explicitly reports pagination/truncation.
  This is an upcoming-event cache, not a complete mailbox/calendar archive.
- Calendar read every five minutes when online; manual refresh is available.
  Access tokens are kept privately in volatile PSRAM until 60 seconds before expiry;
  refresh-token exchange/persistent writes happen only when renewal is needed.
  This avoids unnecessary TLS work and flash writes. Worker jobs use NetworkWorker.
  No app task, second server, LVGL timer handler or C6 firmware is added.
- Atomic snapshot replacement prevents repeated imports from duplicating events.
  Last good data remains on network/parse failures and is restored offline after
  restart. Apps can call `CalendarSyncService::copyEvents(slot, out, capacity)` for
  copied `EventRecord` data and `info(slot)` for status; no token getter exists.
- Password-based CalDAV/IMAP/SMTP, other providers, event writes, attachments and
  a user-facing calendar app are **not** implemented by this module.
- Disconnect removes local tokens/cache, cancelling queued/session generations.
  It does not revoke the provider-side grant: revoke that in Google/Microsoft
  account security settings if required. Deleting/replacing a saved account also
  invalidates/removes its local authorization and queues cache cleanup.

## Security boundaries

Outgoing TLS validates the ESP certificate bundle, requires a valid clock and does
not follow redirects. Provider identity is checked before refresh tokens are saved.
Refresh tokens and Google client secrets are AES-256-GCM encrypted in private
internal Config files; record IDs are authenticated. Never put tokens on SD, in
logs, URLs or app data. JSON/token buffers are wiped before release.

The existing port 8088 administration uses **HTTP Basic**, not HTTPS. Use only a
trusted LAN; for encrypted administration use a separately configured HTTPS proxy.
Secrets cannot be configured through this HTTP portal. OAuth action POSTs require
authenticated admin access plus a random anti-CSRF header; browser state is rendered
using text nodes and provider links come only from fixed/validated providers.

Encryption protects against plaintext UI/export, not a complete physical flash dump:
the device key is in NVS. Secure boot/flash encryption require separate reviewed
eFuse provisioning. File/NVS deletion is logical, not guaranteed forensic erasure.

Hardware/provider end-to-end checks still needed: both login flows, wrong account,
denied consent, token rotation/revocation, restart/offline cache, rapid navigation,
PIN relock during a job and memory/task stack headroom.

Contact synchronization is opt-in per account and requires a new explicit consent:
Google uses People API with `contacts.readonly` (enable People API in the registered
project); Microsoft uses delegated `Contacts.Read`. Existing calendar-only grants
remain valid and are not silently expanded. Configure real client IDs through the
central OAuth setup; no generated or placeholder client IDs authenticate a device.
Imported contacts are read-only; local contacts remain in a separate atomic file.
