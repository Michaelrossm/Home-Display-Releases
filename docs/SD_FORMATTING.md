# Manual SD formatting and hardening

Settings → Speicher → SD-Karte formatieren. The initial button opens warning 1;
two additional, separate confirmations are mandatory. Each step has a one-second
debounce; the entire confirmation sequence expires after 60 seconds. Cancel,
closing settings, switching category, missing SD or missing settings-PIN grant
disarms it. A locked settings PIN opens the native PIN dialog, then the user must
start confirmation again. No Lua format bridge exists.

After final confirmation an existing worker checks authorization again and reads
a SHA-256 fingerprint of the mounted card's boot/volume sector and capacity.
The one-shot request goes through ConfigService, bound to exactly the next
BootManager boot count, then a controlled restart. Even an NVS removal failure
cannot make an old request valid on a later boot.
Actual formatting occurs at startup before app, media, history, file-transfer and
network workers begin. Thus there are no live application SD handles to invalidate
and no internal-storage fallback during a live unmount.

The FAT drive is not guessed: geometry and raw sectors are matched against the
actual mounted SD; multiple/ambiguous matches fail. Its mounted FAT boot sector
and signature are validated. The request is consumed before destructive work.
Only a software reset, matching volume fingerprint, mounted SD and healthy
config/internal storage/update journal permit formatting. Pending update/recovery,
power-on, watchdog/panic reset, changed card, invalid request and failed request
removal all abort. A failure never automatically formats or retries on later boots.

Uses pinned FatFs f_mount/f_mkfs on the proven SD drive, with a 4 KiB heap scratch
buffer, then remounts and recreates the standard Apps directories. No driver
replacement, second SD mount, new task, shell deletion or internal-flash format.
BootManager now explicitly receives an initialized ConfigService before reading
and writing BootHealth counters; rollback and health checks are not bypassed.

Limits: requires an already mountable FAT card with 512-byte sectors. Damaged or
unsupported cards need formatting on a computer; there is intentionally no
format-on-mount-error. Formats the mounted FAT volume, not unrelated secondary
partitions. A bit-for-bit cloned volume cannot be distinguished from its source
by this fingerprint (Arduino's supported API does not expose CID). Quick FAT
formatting is not a secure erase; files may remain forensically recoverable.
No card has actually been formatted during development and no firmware flashed.

Host tests execute the production target resolution/boot-format routines against
simulated SD/FatFs/config/reset backends: two confirmations, debounce/timeout,
millis wrap, abort, permission denial, busy worker, software-reset-only, consumed
request, changed/ambiguous card, raw read failure, NVS removal failure and format
failure without replay. Actual FatFs/card power-loss behavior needs hardware testing.

Further hardening in this change: malformed native PIN verifiers fail closed;
failed unlock-counter persistence revokes the grant and locks for 30 minutes;
expired cooldowns are cleared to prevent signed-millis resurrection; animation
stopping walks iteratively rather than consuming stack proportional to UI depth;
detail-archive records additionally validate source resolution, quality bits,
history flags, infinity and unsupported finite/unknown combinations.

Verification: 105 tests plus architecture checks and source release validation
passed; home_display_p4_legacy_dev compiled successfully. Static RAM 66,296 bytes
(+16 against previous local build), Flash 3,200,534 bytes (+6,276). No additional
permanent task. Physical SD formatting is deliberately not executed by these tests.
