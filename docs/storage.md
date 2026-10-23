# Storage — v0.3.0

## Areas

`StorageService` exposes logical areas: `UserData`, `AppData`, `SystemData`, `Cache`, `Config`, `Recovery`, while preserving the older `StorageClass` API for v0.2.7 callers during migration.

SD is primary for calendar/user files, media, wallpapers, WebApps, logs/backups and imported content. Internal flash contains firmware A/B, NVS, SystemData, AppFS and a small user fallback.

## Atomic write

The central writer uses:

`target.tmp -> flush -> validate -> target.bak -> rename tmp -> validate target -> remove backup`

Boot/recovery can choose valid target, then backup, then temp. Apps do not implement their own atomic sequence.

## Non-destructive mount

SystemData first mounts without formatting. If mount fails and raw partition bytes are not blank, data is preserved and Recovery is entered. Only a provably blank partition may be formatted automatically for first installation.

## Credentials

Radio PIN, IR/power password, calendar credentials and future provider tokens are stored in dedicated NVS secret namespaces, not SD/SystemData/AppFS.
