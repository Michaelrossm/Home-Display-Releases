# PrinterService API v1

The existing schema-2 app and its `printer_profiles` / `selected_printer`
KV keys do not need migration. Firmware reads owner-scoped KV records on the
existing NetworkWorker. Profile changes are reloaded at most every 30 seconds;
opening the profile list requests an earlier reload.

## Lua contract

- `hp_printer_list()`: cached profile array; nil until loaded, so existing
  apps can use their KV fallback.
- `hp_printer_snapshot(id)`: cached table with revision, online/stale/error,
  status/state, nozzle/bed current/target, progress, job times/layers,
  axes, fans and last-command diagnostic. Never performs HTTP on the UI.
- `hp_printer_files(id, directory)`, `hp_printer_macros(id)`: cached
  `{entries, directory, pending, truncated, error, message}`.
  Empty pending lists require a later read/refresh; the root directories are
  prefetched after the first successful status read. Maximum 32 entries.
- `hp_printer_console(id, since)`: last 12 command replies, at most
  512 bytes per reply. The optional second argument is accepted for compatibility.
- `hp_printer_command(id, kind, parameters)`: job ID, or nil/error.
  Use the existing `hp_job_result` / `hp_job_release` API.
- `hp_printer_capabilities()`: version=1, reprap_standalone=true,
  reprap_dsf=true, moonraker=true, octoprint=true; camera/history=false.

Required permissions: `network.local` and `storage`.
Supported command kinds: home, move, set_temperature, set_speed_factor,
set_fan, start_file, run_macro, pause, resume, cancel, gcode.
All use the current app identity; profiles/results cannot cross owners.
Command jobs capture the exact destination and fail if its profile changes.

## RepRapFirmware / DSF

Standalone: `rr_connect?sessionKey=yes`, session header `X-Session-Key`,
`rr_model`, `rr_filelist`, explicit `rr_gcode` and `rr_reply`.
No periodic status G-code and no removed `rr_status` endpoint.
DSF: `machine/connect`, `machine/model`, `machine/directory`,
`machine/code?async=true`. Auto probes DSF only when the standalone endpoint
returns 404/405; optionally set profile `mode` to `standalone` or `sbc`.

The configured printer is contacted only when its owning app first uses the API.
Default password: the printer's standard `reprap`. Optional
`password_secret` refers to the owner's central secret store and requires
`secrets` permission. Passwords/session keys are never returned in Lua status,
profile lists or logs. LAN HTTP is not encrypted; use only a trusted LAN.
Local-address validation and DNS pinning come from the existing HTTP transport;
redirects, URL credentials and Internet destinations are rejected.

## Safety and limits

Movements require a fresh idle model, a homed axis, known soft limits and an
idle millimetre-based HTTP command channel. Relative jogs are limited to 10 mm,
feedrate is bounded and modal positioning/feedrate are restored.
Heating requires the actual model heater/tool mapping, a known temperature
limit and no heater fault. Additional caps: nozzle 350 °C, bed 150 °C.
Thermostatic/unknown fans are protected. Files/macros must remain under
`0:/gcodes/` / `0:/macros/`, without traversal or command injection.
User-selected macros/files are executable printer programs; their contents
remain the printer administrator's responsibility.
Raw G-code allows explicit M115/M119/M105/M114 diagnostics only; all control
must use the checked typed commands. No actuator request is retried automatically.
RRF 3.6 cancellation sends M25 then M0 when not already paused.
HTTP acceptance is reported as status=202 / execution_complete=false,
not as completed movement, temperature attainment or completed printing.

No new permanent task. Two owners, five profiles each, existing four job slots.
One maintenance phase per free NetworkWorker window, at least 500 ms apart;
foreground status ~2 seconds, background ~15 seconds, failures use backoff.
High foreground load defers background profiles; offline/locked state stops HTTP.
Suspension cancels Lua command jobs but retains native monitoring; close, eviction,
disable and uninstall cancel owners and reclaim caches on the worker.
Optional profile `background:false` disables hidden monitoring.
Model response/parser quotas are 64 KiB each, cached snapshots/lists at most 8 KiB.
Each request times out within 1.5 seconds; a maintenance run has a 4-second
deadline and a command run 6 seconds. Queue time is additional.

## Moonraker / Klipper and OctoPrint / Marlin

Profile protocols `moonraker` / `klipper` select the Moonraker adapter;
`octoprint` / `marlin_octoprint` select OctoPrint REST (not direct Marlin serial).
Set the actual server port in the existing profile (usually 7125 for Moonraker).
Both reuse the same bounded worker, owner-scoped jobs and cleanup paths.
Status polling uses object-model/REST endpoints, never periodic status G-code.
Both support nested G-code directories, start/pause/resume/cancel and home.

OctoPrint requires an API key in the owner's central secret store. Store it
through existing `hp_secret_set(key, value)` with `secrets` permission and set
the profile's `password_secret` to that key. Never put the API key in profile KV.
Moonraker uses the same optional reference when its server requires API-key auth.
Keys are not returned in profile/status data. Missing permissions, missing keys
and invalid header values have separate diagnostics.

Moonraker movement requires verified homing/travel/feed limits and an idle
motion channel; Z feed is capped by max_z_velocity. Nozzle heating is blocked
when a different extruder is active. Only configured, non-private macros are
offered/executed; arbitrary G-code cannot masquerade as a macro.
OctoPrint supports checked feedrate changes (50–200%). It cannot provide
authoritative homing/travel/heater limits through the REST status model, so
jogging/heating/fan/macro/raw-G-code controls are deliberately unsupported.
Neither REST adapter claims serial console replies or completed physical motion.
Timeout of an actuator request means unknown outcome; never retry automatically.

Camera streaming, preview thumbnails and persistent history are not implemented.
Boot-time monitoring before an app first opens is also not implemented.

Protocol references:
- https://github.com/Duet3D/RepRapFirmware/wiki/HTTP-requests
- https://github.com/Duet3D/RepRapFirmware/wiki/Object-Model-Documentation
- https://github.com/Duet3D/DuetSoftwareFramework/wiki/REST-API
- https://docs.duet3d.com/User_manual/Reference/Gcodes
- https://moonraker.readthedocs.io/en/latest/external_api/printer/
- https://moonraker.readthedocs.io/en/latest/external_api/file_manager/
- https://www.klipper3d.org/Status_Reference.html
- https://docs.octoprint.org/en/main/api/printer.html
- https://docs.octoprint.org/en/main/api/files.html
