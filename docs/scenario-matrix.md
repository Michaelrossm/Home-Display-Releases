# v0.3.0 scenario matrix

| Scenario | Source/model coverage | Hardware validation |
|---|---|---|
| Cold boot / normal restart | BootManager + BootHealth tests | pending exact board |
| Watchdog/reset restart | reset reason + failed-boot persistence | pending exact board |
| Missing/bad SD | SD optional for boot; asset update preflight refuses unsafe update | pending |
| Wi-Fi/LAN offline | network not part of boot-health commit | pending IP101/C6 port |
| Internet/weather/calendar/IR Tracker offline | explicitly excluded from BootHealth | service runtime pending |
| Invalid WebApp | schema/URL/path unit tests | UI interaction pending |
| SystemData bad | no auto-format; Recovery path | destructive fault injection pending |
| AppFS bad | mountability check blocks commit | fault injection pending |
| old config | schema 1 -> 2 migration test | device NVS migration pending |
| newer config | Recovery, no overwrite | device test pending |
| successful OTA | signed/hash/journal/health path implemented | P4 flash test pending |
| bad firmware/hash/signature | verifier rejects before activation | P4 flash test pending |
| bad asset | hash verify rejects | P4 flash test pending |
| power loss during transaction | dual-record/torn-write model tests | hardware power-cut matrix pending |
| new firmware does not boot | ESP-IDF A/B rollback + failed boot counter | hardware test pending |
| Recovery UI | embedded, no SD/AppFS dependency | touch/display port pending |
| scoped resets | unit-tested actions | UI test pending |
| full factory reset | two-step confirmation; firmware/recovery untouched | destructive hardware test pending |

No hardware-only result is claimed before the exact Waveshare board is available.
