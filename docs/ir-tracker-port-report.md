# IR-Tracker architecture port report

Reference inspected: `Michaelrossm/ir-tracker-offline`, branch `main`, tree `0514bb3ab645bfe0ef9fca888e0389f1eea91482`.

| IR-Tracker source | Home-Display counterpart | Action |
|---|---|---|
| `src/app/core/EventLog.h/.cpp` | `src/core/EventLog.*` | ADAPT — dual-header ring retained; StorageService + 512 records + PSRAM preferred |
| `src/app/network/MinimalMdns.cpp` | `src/services/MinimalMdns.*`, `MdnsService.*` | ADAPT — DNS packet/probe/announce principle; only HTTP/Home-Display services; no amalgamated C3 module |
| `src/app/network/EthernetManager.*` | `src/services/NetworkService.*` | MERGE — reconnect/interface-change/error-state ideas; no W5500 hardware copied |
| `src/app/network/NetworkManager.cpp`, `NetworkStatus.cpp`, `ServicedNetworkClient.h` | `NetworkService`, `NetworkWorker`, `RetryPolicy` | ADAPT — central servicing/backoff, P4 ownership model |
| `src/app/core/ConfigManager.cpp` | `ConfigService`, `ConfigMigration`, `SettingsService` | MERGE — migration/validation concepts; direct global Preferences model not copied |
| `src/app/update/AssetRollback.h` | `TransactionJournal`, `UpdateService`, `BootHealthCheck` | ADAPT — committed/torn-record and power-cut recovery principles retained; fixed C3 offsets removed |
| `src/app/update/AssetRollbackEsp.cpp` | `UpdateService` | ADAPT — partition bounds/hash/boot concepts; C3 asset/debug partition assumptions removed |
| `src/app/update/OtaManager.cpp` | `UpdateService`, `OtaService`, `UpdateVerifier` | ADAPT — verify-before-write and rollback logic; P4 A/B + SystemData/AppFS transaction model |
| `src/app/diagnostics/DiagnosticsApi.cpp` | `DiagnosticsService` | ADAPT — reset/memory/support diagnostics; Home-Display metrics and secret redaction |
| `tools/asset_manifest.py` | `tools/asset_manifest.py` | ADAPT — per-file size/SHA manifest, generalized to SystemData/AppFS |
| `tools/build-asset-image.py` | same path | ADAPT — builds P4 LittleFS asset images instead of fixed 64-KiB IR asset image |
| `tools/build-release-package.py` | same path | ADAPT — Home-Display firmware/SystemData/AppFS package |
| `tools/sign-update-manifest.py` / signing tools | Home-Display signing tools | ADAPT — ECDSA signed manifest, separate product key required |
| `tests/native/asset_rollback.cpp` | `tests/test_update_journal.py`, recovery tests | ADAPT — torn write/power-cut invariant model |
| `tests/native/runtime_regressions.cpp` | storage/EventLog/release tests | ADAPT — failure injection principle |
| `src/app/storage/HistoryStore.*` | existing app histories | KEEP CONCEPT ONLY — meter-specific retention/format not copied |

## Requested older names not present on current IR `main`

The following requested names were **not present** in the inspected current tree, so no path was invented: `ProductSafety.cpp`, `MeterAutoCommissioning.cpp`, `release_validator.py`, and a standalone `supportReportText()` source file. Their relevant current responsibilities are represented by `AssetRollback`, `OtaManager`, `ConfigManager`, `DiagnosticsApi`, tests and build tools.

## Deliberately not copied

Meter/SML/D0, Modbus, Shelly/EcoTracker emulation, GPIO scanning, W5500 hardware details and C3-specific memory compression were not imported.

## Byte-for-byte copies

None. Every imported IR-Tracker implementation required at least namespace/storage/partition/service-boundary adaptation for the P4 project. Proven algorithms and invariants were retained where applicable; C3 memory constraints were not.
