# NAS media — v0.3.1

NAS access is centralized in `NasService`.

## Core transport
WebDAV is the default core transport because it reuses the existing HTTP stack and
can list/stream files without adding a large filesystem client.

## Adapters
- DLNA/UPnP: adapter boundary prepared
- SMB2/3: optional adapter boundary prepared

SMB is intentionally not silently embedded in the core image. `libsmb2` is LGPL-2.1
and should be added as an optional linked component only after release/license review.

NAS credentials are stored only in ConfigService's internal secrets namespace.
Normal profile files contain no passwords.

NAS playback uses the same common audio path and the same global audio focus as SD
music and radio.
