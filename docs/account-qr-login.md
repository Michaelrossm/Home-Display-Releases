# Account QR onboarding

The calendar and phonebook account connector offers QR codes for the existing
Google and Microsoft authorization links. Start authorization first, then open
the QR. A provider device code may still need to be entered on the phone.
The native account settings show the same current authorization URL as a QR.

`GET /api/accounts/login/qr?provider=google|microsoft|apple` requires primary-server
HTTPS and administrator authentication. Only server-owned provider URLs are
encoded, never an arbitrary URL supplied by a client or a saved password/token.
The shared `QrGenerator` reuses the existing LVGL QR encoder, with version 33,
a 2048-byte general payload limit and a 1024-byte account URL limit.
The response contains a bounded row-major hexadecimal bit matrix; the browser
draws it locally with a four-module quiet zone. No external QR service is used,
no extra task or polling loop is introduced. Expired links require a new login.

Apple's QR opens account.apple.com to create an app-specific password. This is
not Sign in with Apple or an automatic Calendar/Contacts authorization grant.
Enter the app-specific password in the HTTPS iCloud form, which uses the existing
encrypted credential store and asynchronous CalDAV setup.

Calendar settings now separate people, account synchronization, calendar mapping
and holidays. Person calendar selections are view filters, not account grants.
The native phonebook opens to a sorted contact list with search/user filter;
details and management are separate views. All existing data services remain.

## Shared generator and local sharing

All account, contact and Wi-Fi QR generation goes through `QrGenerator`.
`matrix()` encodes once; `update()` renders that same matrix to the existing
LVGL indexed canvas with a quiet zone and a minimum two-pixel module size.
There is no independent Wi-Fi/contact encoder and no additional permanent task.

Each phonebook contact has a "Kontakt als QR" action using the existing complete
vCard serializer. Contacts above the QR capacity are not silently truncated:
use the existing `.vcf` export instead. Import handling depends on the scanner.

Network settings list every configured Wi-Fi profile by SSID with a QR action.
The existing Wi-Fi setup QR button also uses saved profile credentials through
the same path. Settings unlock and tablet unlock are checked before reading any
password. Wi-Fi strings use escaped `WIFI:` payloads for WPA or open networks.
The QR contains the password; no server endpoint exports these credentials.
The dialog expires after 60 seconds, closes on locking or owner-view deletion,
and wipes its canvas before freeing. Temporary password/payload/encoder buffers
are wiped; credentials remain solely in the existing network/config services.
