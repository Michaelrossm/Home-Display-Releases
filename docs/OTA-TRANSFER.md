# Firmware transfer hardening

Both the existing HTTP raw-BIN endpoint and signed-package firmware writer use
`OtaWriteSession`. No second update engine, permanent task or Arduino `Update`
instance is created. The IR-Tracker reference is
`src/app/update/OtaManager.cpp`: bounded streaming, cooperative scheduling and
verification before committing. Its C3 hardware, measurement hooks, partition
layout and combined asset format are deliberately not copied to the P4.

- One nonblocking lease covers receiving, writing, readback and HTTP boot-slot
  selection across both web listeners and the package writer. Concurrent uploads
  and reboot requests fail with conflict rather than interrupting a write.
- A 4096-byte **internal, byte-addressable RAM** buffer is allocated before any
  erase. No fallback to PSRAM. Allocation failure leaves flash untouched.
- IDF 5.5.5 `OTA_WITH_SEQUENTIAL_WRITES` erases incrementally. Every write block
  yields one RTOS tick; there is no watchdog disable or blind watchdog feeding.
- Exact length enforcement, 180-second receive deadline and 15-second no-progress
  bound (plus the HTTP server's bounded receive call). Transfer-Encoding is
  rejected; a raw image with Content-Length is required. No automatic retries.
- Disconnect/write/hash failures abort the handle. IDF frees `esp_ota_end` handles
  even on validation failure; these are not aborted a second time.
- Project/chip/revision/version checks, explicit downgrade confirmation, incoming
  SHA-256, exact flash readback SHA-256 and metadata verification remain in place.
  Only then is the inactive slot selected. BootHealth/rollback remain unchanged.
- The signed-package Stream path checks its deadline between bounded `readBytes`
  calls; a synchronous Stream call itself is not preempted by this deadline.
- Browser upload progress distinguishes transmission from device verification.
  Success requires an affirmative response, matching SHA-256 and a slot. Lost
  responses/timeouts never trigger an automatic resend or reboot.
- Web update card has explicit inspect/upload/reboot stages, confirmation before
  upload and reboot, stale-inspection suppression and unload protection. Drop
  events are ignored during transfer. File metadata uses textContent, not HTML.
  Inspection exposes actual OTA capacity and pending boot validation; the same
  SDK hardware-revision check is applied before erase and after readback.
  Factory/merged/bootloader/partition images are not update inputs. HTTPS is
  required by the browser's native SHA-256 API. Reboot HTTP errors stay visible.

Support reports add `ota_last_phase`, `ota_last_bytes`, `ota_last_error`.
Phases: 0 none, 1 begin/erase, 2 writing, 3 image validation, 4 readback,
5 boot selection, 6 ready, 7 failed. A checksummed, fixed 20-byte RTC no-init
record can survive warm panic/reset; it is not guaranteed across power loss.
It is diagnostic only, not a crash backtrace, journal, resume token or boot
authorization. No credentials or firmware payload are retained there.

## Validation and deployment boundary

Host tests execute the actual writer and trace with fault-injected IDF calls:
concurrent lease, internal allocation failure before erase, pending-validation
rejection, running-slot/size protection, bounded writes, short images, write/end
failure, exactly-once cleanup and corrupt retained records. Integration checks
preserve the HTTP auth/hash/metadata/boot-selection ordering.

This hardening is not proof of the cause of the two earlier device panics. It
must first be installed by a known-safe path; an already-running old receiver
cannot be repaired by changing the BIN it is currently receiving. Do not keep
retrying the old crashing receiver. Hardware OTA interruption/low-memory and
warm-reset diagnostics still need validation after initial safe deployment.
