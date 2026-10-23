# P4/C6 performance patch

## Task placement

The existing BSP LVGL task uses P4 core 1. The existing shared NetworkWorker,
blocking background worker and the normal HTTP servers (ports 80 and 8088) use
core 0. The project's custom app_main runs the main orchestration loop on
the IDF main task on core 0; CONFIG_ARDUINO_RUNNING_CORE does not relocate it.
No additional
permanent task, network worker or LVGL handler is created. Radio scan, USB host,
audio, interrupt and ESP-Hosted internal tasks retain their existing policy.
Recovery boot stays unchanged.

`HOMEDISPLAY_SEPARATE_CORES=0` restores unpinned placement for these tasks.
The policy selects core 0 for UI in unicore builds. Existing task priorities,
stack sizes, 1000-Hz tick, display rotation/cache fixes and BootHealth remain intact.

## C6 use

The C6 continues running the pinned ESP-Hosted radio firmware. The existing
central NetworkService temporarily requests `WIFI_PS_NONE` while queued/active
worker jobs or native HTTP transfers are present, plus five seconds after activity.
Afterwards the configured modem-sleep policy resumes. Calls remain in the
normal NetworkService loop, not in workers or Lua callbacks. Atomic hints and an
HTTP scope guard cover cancellation/error returns and long maintenance transfers.
Radio scanning, WLAN-off schedules and LAN preference retain their existing rules.

This is a real radio policy, not remote general-purpose computation. It trades
some power during transfers for lower modem-sleep latency. No C6 flash is needed.

## Why no C6 webserver/compute offload

The pinned ESP-Hosted implementation offers optional Network Split, but it is
not enabled in this project. It requires matching host/slave port routing and
an application on the slave; merely enabling the switch does not move P4 HTTP
handlers, credentials, StorageService or CSRF there. The C6 has no existing
general compute RPC. Moving the current webserver would duplicate these services
and would not accelerate wired IP101 Ethernet, which terminates on the P4.

Reference: [Espressif Network Split](https://github.com/espressif/esp-hosted-mcu/blob/v2.12.13/docs/feature_network_split.md).

## Validation and limits

Native tests cover core selection, compile-time fallback, radio holdoff and
millis wraparound; architecture tests check reuse of existing tasks and central
radio calls. Support reports expose configured cores and the burst flag, not
fabricated CPU percentages. Build success does not establish faster FPS or network
throughput: measure on the exact board before claiming a speedup. These changes
do not preempt a running HTTP upload to serve queued jobs; the shared queue remains
bounded and serialized for safety.

## Live-app fairness

Maintenance can run between queued jobs, but not inside an active transfer.
Expired printer control reservations are still retired only with an empty queue.
Foreground status targets a one-second start-to-start cadence with a 250-ms
minimum pause after slow rounds. Failed/offline/background backoff is unchanged.
RRF caches only printer identity and firmware version for 30 seconds; motion,
HTTP-channel safety, temperatures and tools are still fetched each round.
Lua soft-budget throttling scales to the requested tick (250-ms floor, 1000-ms
penalty ceiling) without relaxing hard time/instruction limits or quarantine.
Slow or non-cooperative individual HTTP jobs still cannot be preempted.

## Shared-system hot paths

Settings snapshots use an immutable, revision-checked cache; successful central
configuration/secret changes and resets invalidate it. Cache publication holds
no lock across NVS reads. Unchanged correctly typed values skip flash writes;
failed writes do not publish a new revision. App-enable reads cache the full
identity record and bounded legacy/default results; successful mutations invalidate
them. Migration and installer rollback semantics remain unchanged.

Boot event logging remains synchronous. Before the normal UI starts, runtime
logging switches to a 16-record queue on the existing blocking worker. Two records
are persisted per scheduled turn; RAM ring access is synchronized and callbacks
run outside the ring lock. Runtime add() acknowledges queue acceptance, not a
completed flash commit. Power loss may lose queued diagnostics; queue overflow
or disk failures are exposed in support reports. BootHealth and transaction
journals are not queued or bypassed.

The calendar main-loop tick and account-event publisher defer on mutex contention.
File-server expiration queues its potentially slow shutdown on the existing
NetworkWorker and rechecks the current session before stopping. Busy jobs may
delay shutdown. Settings scans outside the Apps page are throttled; explicitly
entering Apps invalidates this throttle immediately. Unchanged C6 sleep policy
no longer queries radio mode every loop. UI resource/navigation/callback/
housekeeping phase maxima are available in support reports.

No permanent task or second network/LVGL implementation was added. These changes
do not make individual Lua scripts, protected storage commits, cold package reads
or long HTTP transfers preemptible; interactive hardware measurements are still
required before claiming an end-to-end latency/FPS improvement.

## Verified device run

194 host tests and architecture checks passed. The source release validator
passed its tests but still rejects the existing default-password literals in
RecoveryWebService and WebServerService; they were not changed or suppressed.
The legacy 360-MHz build succeeded and was flashed with hash verification to
the confirmed P4 revision v1.3 on COM7. No AppFS/SystemData images were uploaded.
Static build usage: RAM 63,440 bytes, flash 5,163,168 bytes (not an isolated
before/after performance delta).

A 45-second USB boot capture confirmed multicore startup, 360 MHz, LVGL
configured on core 1 and the NetworkWorker actually executing on core 0.
Post-start internal free heap was 206,808 bytes; no panic or subsequent P4
reset appeared in that capture. C6 SDIO streaming handshake succeeded.
The existing locked host component is 2.13.0 while the installed C6 is 2.12.13,
and the runtime reports a version warning. No automatic C6 update was performed.
The capture also contained a USB device unit-attention message (sense 0x06,
ASC 0x28); USB storage availability was not established by this boot capture.
Display appearance, FPS, radio-policy transitions and sustained throughput
still require an interactive device test; they are not inferred from boot success.
