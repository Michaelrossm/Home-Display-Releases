# Phonebook lifecycle, names and IP access

The phonebook root uses event bubbling for keyboard dismissal. Its deletion
callback now defensively compares the event target against its own root.
LVGL 9.3 already excludes DELETE from bubbling, so this guard is preventive,
not a proven root cause of the reported reboot. The editor also clears stale
field handles before constructing new controls.

Phonebook contacts store `firstName` and `lastName` alongside the compatible
`name` display field in the existing atomic JSON file. Users share the calendar
identity, with structured name parts stored in the existing `cal_person_ui`
options keyed by the full eight-digit person identity. No binary PersonRecord
layout, account identity or calendar ownership changes. Existing display names
remain intact in the first field until explicitly edited; no surname is guessed.
Names are bounded and control characters rejected. vCard `N` imports/exports
preserve first and family name in addition to `FN`.

Native and browser contact/person forms expose separate first/last fields.
Combined calendar display names retain the existing 39-byte limit; contact
display names retain the existing 256-byte limit.

The port-80 HTTPS redirect uses the accepted socket's local IPv4 address, with
the central NetworkService IP as fallback, never a supplied Host header. Thus
typing the IP no longer redirects to a `.local` name the client cannot resolve.
The existing self-signed TLS certificate still needs browser trust approval;
using an IP can also trigger a hostname mismatch. HTTPS is not disabled.

The single MinimalMdns implementation joins/sends on the selected active IPv4
interface and honors QU/unicast requests. MdnsService refreshes announcements
after an IP/path change, checked once per second, with existing retry handling.
Client mDNS support and multicast filtering remain network/OS dependencies;
IP access is the fallback and doesn't depend on mDNS.

USB observation did not reproduce the reported reboot. The defensive lifecycle
behavior is covered by an executable child/root deletion test. The actual reboot
cause remains unconfirmed without a failing USB panic/watchdog/reset trace.
Hardware validation remains outstanding until flashing and reproducing on-device.
