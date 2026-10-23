# Native SD detail history

Local implementation; disabled by default. The existing compact history and its
internal-storage fallback remain unchanged. No new task, no Lua file-writing loop.

## Configuration

`SettingsSnapshot` / `SettingsService` persist:

- `energyDetailHistoryEnabled`: false
- `energyDetailHistoryIntervalMinutes`: 5; valid 1–30, invalid values reset to 5
- `energyDetailHistoryRetentionDays`: 0 = unlimited; otherwise explicit day retention
- `energyDetailHistoryPreferProviderBackfill`: true

Settings are prepared natively, not exposed through an app-specific write bridge.
Missing SD keeps configuration and bounded RAM samples, but never writes detailed
data to internal flash. Excess oldest points are dropped and counted.

## Data and filesystem

Through StorageService, SD UserData only:
`energy/detail/YYYY/MM/YYYY-MM-DD.bin`. Day names use configured local time;
record timestamps/buckets use UTC, so the repeated DST hour stays unambiguous.

Binary v1 is little-endian, IEEE754. File header: 8 bytes. Block header: 16 bytes,
with magic, count, payload CRC32 and header CRC32. Record: 40 bytes, containing
UTC seconds, seven floats, configured/effective interval, true source resolution,
per-field quality and history-origin bits. Unknown values are NaN, never fabricated
zeroes. Power fields are weighted means; SOC and cumulative counters keep the last
valid reading. Missing buckets are not manufactured.

Normal writes append bounded blocks approximately every 30 minutes. The current
unfinished bucket remains buffered. Readable valid prefix survives incomplete or
CRC-damaged tails; only the damaged tail of an owned archive file is truncated.
Invalid file headers are not formatted or replaced. Quality upgrades rewrite only
affected blocks, using a CRC-protected redo journal before touching the original.
Replay is idempotent after interruption. No whole archive rewrite.

Retention deletes only validated, correctly named archive day files older than the
explicit cutoff. Unlimited means no automatic deletion. Free space below 16 MiB
is reported; a full card retains bounded pending data and counts failed writes.

## Provider backfill

Optional `EnergyHistoryProvider` defaults to unsupported. IR-Tracker uses at most
two existing `range=hour&anchor=...` requests and filters the most recent 40 minutes.
Its real `step` is retained. One-minute points aggregate into five-minute records;
15-minute history stays coarse, not expanded into fictional one-minute records.
Only measured grid power is imported: interval import/export from that API are not
cumulative meter counters. Other fields remain from real live providers or NaN.
Backfill can upgrade matching live records, including already archived ones;
repeated points are deduplicated. No interpolation or extrapolation.

## Status, export and cost

Energy-permission Lua receives read-only `hp_energy_detail_history_status()`:
enabled, sd_available, interval_minutes, retention_days, buffer_points,
buffered_minutes, last_flush, last_backfill, backfill_supported, backfill_source,
stored_days, bytes_used, free_bytes, low_space, dropped_points, write_errors,
recovered_blocks, flush_bytes, flushes and aggregation/flush timing diagnostics.
Status reads use a nonblocking cached mutex; callbacks never touch the SD or HTTP.
Native `visitDetail(from,to,visitor)` provides bounded chronological reads for a
future streaming CSV/JSON export. It must run on a worker, never the UI thread.

Lazy allocation in PSRAM contains a 96-point aggregation buffer, 2048-entry bounded
day index and two 96-record scratch blocks: approximately 45 KiB, independent of
archive length. No allocation while sampling. No per-app FreeRTOS task.
At five minutes: 288 × 40 = 11,520 payload bytes/day, approximately 0.34 MiB/month
and 4.02 MiB/leap-year, plus headers and occasional quality-upgrade journals.
Nominal five-minute flush: six records + header = 256 bytes, up to 48 flushes/day;
backfill overlap/upgrades can add bounded writes. CPU aggregation microseconds,
samples, total written bytes and maximum maintenance duration are instrumented.
Maximum maintenance duration includes provider HTTP, not just SD access.

## Verification and limits

Host tests execute production archive/aggregation code with simulated SD, clock
and provider. They cover bounds, averaging, counters/SOC, NaN, UTC keys, resolution,
CRC/tail recovery, no-SD, delayed flush, export, overlap dedup, write failures,
explicit retention and calendar rollover. Hardware timing and physical SD
power-loss behavior require a separately authorized hardware run.

No existing general shutdown-flush service was available; power loss may lose
pending RAM samples. A compatible provider's recent history can backfill them.
Changing interval affects new samples; old records retain their recorded interval.
Index limits intentionally bound RAM; a pathological day exceeding 2048 records
fails safely rather than truncating valid data. Minimum normal interval is one
minute (1440 points/day, with margin for transitions and DST).

New files: EnergyDetailCore.h, EnergyDetailArchive.h, this document and native
archive tests. Integration is confined to energy/power services, central settings
and storage, AppServices registration, and the read-only Lua energy bridge.

Verified legacy image before/after detail history (PIN already included): static
RAM 67,304 → 67,512 bytes (+208), Flash 3,150,986 → 3,182,644 bytes (+31,658).
Measurements are compiler image metrics, not runtime CPU or card benchmarks.
