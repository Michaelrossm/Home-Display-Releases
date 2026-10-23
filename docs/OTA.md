# OTA / update design — v0.2.7

## Network behavior

OTA is not tied to Wi-Fi.

`NetworkService` selects:
1. Ethernet/LAN
2. Wi-Fi fallback
3. Offline

The update service only asks whether a network path is online. Therefore the same
A/B firmware update can be downloaded via LAN or Wi-Fi.

## Safe A/B flow

1. Device runs firmware A.
2. New image is downloaded into inactive firmware B.
3. Image integrity/signature checks must pass.
4. Boot partition changes to B.
5. Device reboots.
6. B starts in `PENDING_VERIFY`.
7. Core services, apps and UI must initialize.
8. HomePanel waits a 30-second stability window.
9. Only then is B marked valid.
10. If B crashes/reboots before validation, bootloader rollback can restore A.

## Hardening requirements for the downloader

The manifest/downloader implementation must:
- prefer HTTPS;
- verify TLS using a CA/certificate bundle, never `setInsecure()` in production;
- check product/hardware compatibility;
- reject downgrade unless explicitly authorized;
- enforce maximum image size for the selected OTA slot;
- verify SHA-256 before accepting an image;
- later verify a detached signature/public-key signature;
- write only to the inactive OTA slot;
- never erase the currently running slot.

The partition/health-validation side is already prepared in `OtaService`.

## LAN

A wired update is fully supported by the architecture. Once the exact Waveshare
IP101 Ethernet board port is enabled, LAN automatically has priority over Wi-Fi.
