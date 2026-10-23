# Update and Recovery — v0.3.0

## Transaction states

`Idle -> PackageVerified -> FirmwareWritten -> BackupReady -> AssetsWriting -> AssetsVerified -> BootPending -> HealthChecking -> Committed`

Any unrecoverable intermediate condition becomes `RecoveryRequired`.

The transaction journal uses two CRC-protected NVS records with monotonically increasing sequence numbers. A torn/new invalid record cannot replace the older valid one.

## Package verification

Runtime update validation checks product, schema, semantic version, image sizes, SHA-256 and ECDSA manifest signature. Production releases require a dedicated Home-Display public key.

## Asset safety

SystemData/AppFS replacement requires:

1. verified package
2. target partition/size validation
3. SD capacity check
4. full partition backup
5. backup SHA-256 verification
6. journal `BackupReady`
7. only then erase/write target
8. verify written partition SHA-256

A backup is re-hashed again before Recovery erases a damaged target.

## Boot health

Required for commit:

- config compatible
- SystemData available
- AppFS mountable
- UI initialized
- LVGL frames progress
- touch port ready
- event loop progress
- SystemSupervisor running
- internal heap >= safety floor
- no bootloop safe-mode

Not required: WAN, weather, calendar server, IR Tracker, internet radio or SD.

## Recovery UI

Recovery is embedded in firmware and loads without SD/AppFS. It shows firmware/reset/update/storage/config status and provides scoped restart/rollback/asset/network/UI/WebApp/calendar/media recovery actions. Full factory reset remains separately confirmed and never erases firmware/recovery partitions.
