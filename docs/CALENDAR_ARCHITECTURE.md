# Calendar architecture — v0.2.7

## Identity

Each family member is identified by a normalized e-mail address.

The firmware hashes the normalized e-mail into a stable 32-bit local person ID.
The e-mail itself remains stored for display and CalDAV account mapping.

Default capacity:
- up to 16 persons
- up to 32 calendars
- 16-bit event participant mask
- 32 upcoming events cached in RAM

The UI is designed for 4+ family members.

## No duplicate events

An event is stored once.

Participants are a 16-bit mask referencing the person table. A family appointment
with four participants still occupies a single EventRecord.

Birthdays are never duplicated into the event database. Birthday date belongs to
PersonRecord and the calendar creates virtual read-only birthday events at runtime.

## Storage

Primary:
`/homepanel/data/calendar/` on microSD.

Files:
- `persons.bin`
- `calendars.bin`
- `events.bin`
- `meta.bin`

If SD is absent, the small StorageService UserData fallback can keep limited data
internally. Large media/history still never falls back to flash.

The calendar files use compact fixed-size binary records rather than loading a large
JSON document. Upcoming events are scanned into a maximum 32-record RAM cache.

## CalDAV preparation

Every stored event already has:
- stable UID
- revision
- ETag derivation
- href derivation
- global sync revision

Prepared logical paths:
- `/dav/principals/<person-id>/`
- `/dav/calendars/<person-id>/`
- `/dav/calendars/<person-id>/<calendar-id>/`
- event `.ics` resources

`CalDavCore` generates RFC5545 VCALENDAR/VEVENT data directly from EventRecord.
There is no second CalDAV database.

The transport layer still needs the HTTP/WebDAV method implementation
(OPTIONS, PROPFIND, REPORT, GET, PUT, DELETE) and TLS certificate provisioning.
This is intentionally kept separate so the project does not add a second web stack.

## External providers

SyncAccountRecord reserves providers for:
- HomePanel local
- generic CalDAV
- Google CalDAV
- iCloud

Google CalDAV requires HTTPS + OAuth 2.0. OAuth tokens/secrets must be stored only
in internal SystemData, never in the SD calendar records.

## Credentials

Local CalDAV credentials are keyed by person ID.

CalendarAuthService stores:
- 128-bit random salt
- 4096-round SHA-256 derived password hash

No clear-text CalDAV password is stored.
Minimum local password length is 8 characters.

This is a compact embedded credential mechanism; HTTPS remains required before
exposing the service beyond a trusted local network.
