# HTTPS and account setup

Normal management is served over HTTPS on port 443. Port 80 has only a GET
redirect to the configured mDNS hostname; it has no login, POST or upload API.
The optional maintenance server also uses TLS on port 8088. The independent
emergency recovery server is unchanged and is not an account-provisioning server.

Each device generates its own P-256 private key and self-signed certificate on
first use. The identity is stored through the encrypted CredentialStore. Damaged
identity files/backups are not overwritten. The certificate covers the configured
hostname followed by `.local`; use that hostname instead of the numeric IP.
Changing the hostname later requires a corresponding certificate replacement.

Before entering credentials, compare the browser certificate SHA-256 fingerprint
with the `[HTTPS] certificate SHA256=...` line obtained through trusted USB. Then
trust that certificate locally. A browser warning must not simply be ignored.
The certificate expires in 2036; local certificate renewal/import is not yet a UI
feature. TLS admission is limited to two main clients and one maintenance client.

`/accounts`, calendar and phonebook offer Google/Microsoft OAuth through the
existing account services. Provider registration/client IDs must be valid; HTTPS
does not replace those registrations. iCloud uses an Apple app-specific password;
custom CalDAV accepts its server credentials. A blank password reuses the stored
credential. Passwords are sent only through HTTPS, never returned in status JSON,
and the worker-owned password buffer is wiped on completion or queue rejection.
PIN, administrator authentication, CSRF and capability checks remain required.

Contact synchronization stays opt-in. No additional account/network worker is
created. Compilation and host tests do not constitute browser/device validation.
