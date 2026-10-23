# microSD policy — v0.2.7

## Goal

Use microSD as the primary high-capacity user-data store. The panel must still boot
without a card.

Target: **128 GB SDXC cards and larger where supported by the controller/card**.

## Filesystem

For maximum compatibility with ESP-IDF/FatFs, HomePanel targets FAT32.
Many 64/128/256 GB SDXC cards ship as exFAT and may need to be reformatted to FAT32
before use.

The exact SDMMC pin/bus initialization is kept inside the board hardware port and
must match the exact Waveshare D-board revision.

## Directory layout

/homepanel/
  data/
    calendar/
    family/
    weather/
    power/
    radio/
  apps/
  backups/
  media/
  intercom/
  voice/
  logs/
  import/
  export/

## Storage classes

- SystemData: internal only
- AppPackage: internal AppFS later
- UserData: SD preferred, small internal fallback
- Media: SD only
- Logs: SD preferred; dropped if unavailable rather than filling flash
- VoiceModels: SD only

Apps never mount SD or LittleFS directly. They use `StorageService`.
