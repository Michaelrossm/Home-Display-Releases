# Generic Lua lifecycle and background infrastructure

Local firmware only. Existing apps were not migrated and no new example app or
weather/calendar/radio/power background job was added for this feature.

## Runtime

`LuaRuntimeState`: Foreground, Suspended, Evicted, BackgroundExecuting.
Foreground is the visible runtime. On app/Home transitions, normal Lua states are
parked under a hidden system-owned bottom-layer container. Their existing timer is
paused, object animations removed, ordinary UI/events gated, and normal asynchronous
jobs cancelled. No ticks or event delivery to suspended apps. Native apps keep
their existing close/open behavior.

Resume reparents the same root, keeps the same Lua state/bindings and reactivates
the existing timer. Optional `on_suspend`, `on_resume`, `on_evict` callbacks use
10 ms / 50 hook-call budgets (the runtime's existing instruction hook); legacy
`on_pause` remains supported. User code must not rely on the completion of normal
jobs cancelled during suspension. Already-running network I/O is cooperatively
cancelled using its existing bounded timeout, not forcibly killed.

Three suspended main states maximum, oldest first. Additional pressure threshold:
internal 8-bit free heap <64 KiB or PSRAM free heap <2 MiB. Check on transitions,
not through another polling task. Foreground is never a victim. Genuine close,
uninstall and eviction cancel jobs and release state/UI/bindings. Settings states
always close when left. Entering an app's own settings replaces its main state,
as before; main/settings globals remain separate. No state-persistence fiction.
Stopped LVGL animations are not reconstructed automatically on resume.

## Policy/API

Schema-2 optional `background` object:

```json
"background": {
  "enabled": true,
  "priority": "low",
  "preferredIntervalMinutes": 180,
  "maximumDelayMinutes": 360,
  "requiresNetwork": true,
  "allowDuringHighLoad": false
}
```

Missing object means disabled. Nested keys/types strictly validated, unknown keys
rejected; old schema-2 manifests remain valid without changes. Allowed priorities:
None, Low, Normal, Important; no Critical. Minimum intervals: Low 30 minutes,
Normal 15, Important 5. Maximum interval/delay: seven days; delay >= interval.
Package/Lua Important requests are downgraded to Normal and disallow high-load
execution; the effective interval is raised to at least 15 minutes. Only trusted
firmware registration can approve Important.

`hp_background_capabilities()` reports version=1, policy=true, callback=false,
max_suspended=3 and small cached lifecycle/scheduler counters.
`hp_background_request({priority="low", preferred_interval=10800,
maximum_delay=21600, network=true, allow_high_load=false})` registers interest,
in seconds. It never invokes a callback or starts network work. Bad/too-short
values fail. Settings states cannot register interest. Policy disappears on real
close/eviction/uninstall and native manifest policy re-registers on normal open.

## Scheduler and explicit safe limit

AppBackgroundScheduler stores at most 32 metadata entries, no task. It registers
one minute-resolution metadata pass with the existing BackgroundScheduler and
uses its sole Blocking worker for selection. Network-required execution goes to
the existing NetworkWorker, never a second HTTP queue/client/task. One pending
execution is allowed, with cancellation/recheck on close, foreground, load or
offline changes before it starts.
Trusted native handlers can be registered through `registerNative()`; none are
registered by this change. They must be short/bounded and never touch Lua/LVGL.
Handler is stateless owner-ID based; no dangling Lua-state pointer crosses tasks.

Idle permits work; Normal prefers Normal/Important and opportunistically Low;
High defers Low/Normal and permits only approved Important + allowDuringHighLoad.
Offline defers network-required work. Foreground owner is skipped. Priority,
overdue age and soft maximum-delay guide selection, with at most one job per
minute pass and no catch-up storm. No polling loop on offline/deferred jobs.
`setForegroundLoad(Idle/Normal/High)` is a simple firmware API; transitions reset
load to Normal, not a CPU profiler. Network-required jobs remain serial and close
due jobs naturally share the existing worker/network phase, without parallel HTTP.

Lua `on_background` is deliberately NOT executed or registered. The many native UI
bridges do not yet have a complete background-safe gate. BackgroundExecuting is
reserved for a future safely isolated Lua execution path, not falsely reported
today. No fake job or partially protected background Lua callback. Existing Lua
hard instruction/time budgets are retained and tested; native C++ handlers cannot
be asynchronously preempted by a Lua instruction hook.

## Tests, files and cost

Host fixture runs production AppManager, Lua suspend/resume methods, LRU and
scheduler selection against simulated LVGL/services and Lua-state identity.
It checks A/B/resume reuse, settings closing, LRU/pressure eviction, disabled apps,
uninstall cancellation, High/offline deferral, catch-up, priority approval and
actual background-manifest parsing. Existing real-Lua sandbox/allocator/time and
instruction-budget tests remain. Physical touch/display behavior is not validated
by host tests or compilation.

New production headers: `core/AppLifecycleCore.h`, `core/AppBackgroundScheduler.h`.
Implementations live in existing AppManager/BackgroundScheduler/LuaApp and common
navigation bridge. Registry/manifest transport policy; UiShell uses suspension on
Home; AppServices registers the metadata service. No app scripts altered for this
feature. Tests: `test_app_lifecycle.py`, `native/app_lifecycle.cpp`; existing
ownership regression checks now expect the centralized suspend-or-close path.

No new permanent FreeRTOS tasks: task creation sites and the sole Blocking worker
are unchanged. Retained states consume dynamic PSRAM/UI memory, bounded by three
states, each Lua allocator's existing 1 MiB limit and existing native/UI limits.
Baseline immediately before lifecycle: static RAM 67,512 bytes, Flash 3,182,644
bytes (legacy build, PIN + SD archive included). Verified final build reports
static RAM 66,280 bytes and Flash 3,194,258 bytes: compiler-reported differences
−1,232 bytes and +11,614 bytes. This is a linked-image comparison, not a claim of
lower runtime heap usage: retaining states increases dynamic memory deliberately.
103 tests, architecture checks and source release validation passed. Runtime
heap/high-water and callback timings need a separately authorized hardware run.
