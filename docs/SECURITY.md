# Native PIN protection

Settings and tablet PINs use independent native SecurityService states and
credential keys (`sec_set_pin`, `sec_tab_pin`). Both default to disabled.
PINs contain 4–12 decimal digits; setting/changing requires confirmation,
changing/removing an existing PIN requires its current PIN. Removing is explicit.
Only a random salted SHA-256 verifier (2048 additional rounds), not the PIN,
is stored in ConfigService's credential namespace. There is no PIN getter or
tablet unlock/management Lua API. The existing NVS is not encrypted at rest;
physical extraction and offline guessing of short PINs are not prevented.

Each PIN has its own persisted failures and cooldown. Failures 5/10/15/20
block for 120/300/600/1800 seconds. At 20 or above, every further incorrect
attempt blocks for 1800 seconds. A blocked attempt does not extend the running
cooldown. A successful verification resets only that PIN's failures.
Changing and removing share its attempt counter. Reboot restores the full
last recorded cooldown conservatively (no trusted clock needed); rebooting
cannot clear the attempts. Failure to persist attempts fails closed.

Settings grants are revoked on app close, Home, navigation to another settings
category, tablet lock and reboot. RadioSettings holds the security mutex across
its save and preserves all six protected values while locked: parental control,
volume limit enabled, maximum volume, source lock, power-off lock and radio
control lock. Host/FSAPI credential saves remain possible. Async jobs recheck
the native authorization when actually saving, not just when Lua requests it.

Tablet lock starts on boot with an enabled PIN and after display-Off by default;
ordinary dimming does not lock. The central security page can lock manually and
configure the Off trigger. A native opaque system-layer keypad consumes touches;
Lua apps pause, app requests are discarded, media/radio command entry points and
queued Lua jobs reject requests while locked. Existing backend monitoring remains
active. No task or LVGL handler is added.

Settings Lua API: `hp_security_settings_status()` returns enabled/unlocked/
blocked_seconds. `hp_security_request_unlock_settings()` requests the native
keypad, `hp_security_unlock_settings(pin)` verifies settings PIN only, and
`hp_security_lock_settings()` revokes the grant. Fitz Touch 2.3.0 uses the native
keypad, keeps protected controls visible/disabled at 50% opacity, and declares
only `media` permission. Its main.lua remains a separate Lua state.

Host tests run the production service with clock, credential and crypto doubles:
PIN validity, confirmation, independent PINs, lifecycle, escalating cooldowns,
boundary times, restart, failed persistence and secret verifier storage. Firmware
compilation is separate from physical touchscreen/NVS validation.
