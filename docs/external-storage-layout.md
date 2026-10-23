# External volume layout

Healthy mounted SD and USB volumes now reuse StorageService's shared preparation
for `/Apps`, `/Apps/32`, `/GCode` and the existing `/homepanel/data`, `media`,
`logs`, `voice` roots. The native USB mount manager calls preparation after a
successful mount and after an explicitly confirmed format. SD does so at boot
mount and after its explicitly confirmed boot format. There is no new task,
periodic filesystem scan or UI-lock filesystem work.

Creation is idempotent and bounded. Existing directories and files are retained;
a file occupying a required directory path causes preparation to stop, never an
overwrite, delete or implicit format. A read-only/full/removed volume remains a
reported failure rather than triggering recovery formatting. USB filesystem
generation checks and the existing file guard remain active. Directory mutation
invalidates the existing storage cache revision.

The file explorer and file web page already enumerate the USB root; the observed
USB page contained only a system directory because creation was missing, not
because a browser filter hid the standard layout. On an existing drive the new
layout is applied on its next successful mount with the updated firmware.
No files are migrated and no directory is created on the attached hardware until
that firmware is actually run. Installable Lua apps do not create privileged
system/package roots themselves.

The old build also selected FATFS_LFN_NONE (8.3 names only). That is incompatible
with `/homepanel` and longer app/G-code names. Heap-backed long filenames (255)
and the UTF-8 FatFS API are enabled in defaults; existing short-name files remain
readable without reformatting. The generated target configuration must match.
