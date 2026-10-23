# Flash partitioning — VNext 3.1

The 32 MiB internal flash is now optimized around **A/B OTA + apps + small system data**.
Large/changing user data belongs on the optional microSD card.

| Partition | Size | Purpose |
|---|---:|---|
| NVS | 24 KiB | settings / small critical state |
| OTA metadata | 8 KiB | active A/B slot |
| PHY init | 4 KiB | PHY calibration data |
| ota_0 | 9 MiB | firmware slot A |
| ota_1 | 9 MiB | firmware slot B |
| appfs | 9 MiB | installable apps / app assets |
| systemdata | 4.8125 MiB | internal system data + emergency fallback |
| coredump | 64 KiB | crash diagnostics |

The table exactly fits the 32 MiB address space with the core-dump partition ending at `0x2000000`.

## Migration warning

This layout is NOT an ordinary OTA update. `ota_1`, `appfs` and `systemdata`
move. Do not flash it over the old layout without an externally verified
backup of apps, settings, credentials and system data and an explicit migration
plan. Old filesystem images cannot simply be restored at the new offsets with
different sizes. Export/import their files with the matching firmware. Keep a
known-working recovery image and the old partition table. No automatic format
or erase is introduced. NVS, OTA metadata, PHY and coredump addresses are unchanged.

Each firmware image must fit 9 MiB, including future OTA updates. Compilation
alone is not a storage migration or hardware validation.

## Why two firmware slots?

Updates are written to the inactive slot. The previous working image stays untouched.
On a bad boot, rollback can return to it.

The OTA mechanism is independent of the network interface:
- Ethernet/LAN is preferred.
- Wi-Fi is automatic fallback.
- Both write the same inactive OTA partition.

## Why systemdata is small

Calendar history, power history, images, intercom recordings, voice models, logs and
other large user data are SD-first. Internal storage is only a safe fallback for
small data needed to keep the panel usable if the card is removed.
