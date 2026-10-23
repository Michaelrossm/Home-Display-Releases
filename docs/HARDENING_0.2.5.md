# Hardening — v0.2.5

- Fixed TimeService `void`/`bool` startup API mismatch.
- E-mail normalization rejects whitespace/control characters and malformed local IDs.
- Person IDs are stable hashes of normalized lower-case e-mail.
- Calendar limits are fixed and checked before writes.
- Event participants cannot exceed 16 persons by design.
- Birthdays are derived from person data; no duplicated birthday records.
- Local CalDAV passwords are never stored in clear text.
- Salted/iterated hash comparison is constant-time style.
- Provider tokens are architecturally separated from SD user-data records.
- CalDAV protocol metadata reuses EventRecord rather than introducing a second store.
- Calendar background work uses the existing shared worker.
- Atomic binary writer added to StorageService.
- No provider is allowed to write directly to SD/NVS outside central services.
- Full CalDAV transport is not falsely enabled before HTTPS/WebDAV handling is complete.
