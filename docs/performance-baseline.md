# Performance baseline — v0.3.0 source stage

Real boot time, FPS, LVGL frame duration, heap/PSRAM minima, SD latency and audio underruns require the actual Waveshare D-board and are intentionally not fabricated.

Instrumentation now exists for internal/free/min/largest heap, PSRAM free/min, network reconnects, storage errors, supervisor stalls and NetworkWorker queue rejects in `DiagnosticsService`.

Source-level changes compared with v0.2.7:

- blocking MiniBrowser HTTP removed from UI callback
- Radio media commands moved out of UI callback
- one network worker, no task per app
- legacy unused radio storage modules removed
- EventLog allocation moved to PSRAM when available
- update/backup buffers allocate PSRAM first
- `main.cpp` reduced to orchestration

Hardware before/after numbers must be collected after the exact DSI/touch/Ethernet/audio board port is enabled.
