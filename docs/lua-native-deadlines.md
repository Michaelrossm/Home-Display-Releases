# Completed foreground callbacks and native latency

Device observation: printer settings failed `on_open time_budget`; after sliced
construction, `on_tick` and even the empty framework `data.step` could quarantine
the app. A diagnostic USB capture showed successful coroutine returns taking
37–40 ms. These are observations, not proof of a specific PSRAM/flash stall.

`protectedCall` previously converted every successful return past 5/12/20 ms
into `LUA_ERRRUN`, indistinguishable from runaway Lua. The completed operation
was not undone, and the owner was quarantined. No operation should be retried.

The post-return policy now accepts completed Opening/Foreground calls up to
100 ms, reports `LUA_NATIVE_SLOW` at most once per five seconds, and preserves
the measured duration for existing tick backoff. Over 100 ms remains fatal.
VM instruction-hook errors (instructions and wall time) are unchanged. Suspended,
ownerless and BackgroundExecuting calls retain their original post-return limit.
This is a foreground native-completion tolerance, not preemption of C bridges.
Native bridges can still block until they return and must remain bounded.

No new tasks, CPU boost, retries, network commands or persistent format changes.
An explored extra PerformanceGuard was removed: the BSP scanout already holds
its performance lock. PowerManager behavior is unchanged.

Tests execute the actual protectedCall with bounded host doubles, covering
completed foreground calls, rate-limited diagnostics, unchanged VM error return,
100-ms ceiling and strict background/suspended/ownerless deadlines. The existing
pinned-Lua runtime fixture still verifies real instruction-hook termination.

Printer package 0.1.9 retains the smaller 1220x664 layout, removes root padding,
separates settings load/layout steps, and builds at most six positioned widgets
per tick, yielding earlier once a 3-ms slice is exhausted. Existing credentials,
app ID and KV keys remain unchanged. No printer control is part of validation.

Follow-up: soft tick throttling uses 5% of the requested interval (4–8 ms)
instead of a fixed 4 ms, and begins after three consecutive overruns, not one.
The VM hard deadlines remain unchanged. Package 0.1.10 hides incomplete startup
and page trees while constructing them, then reveals them once. This avoids
visible card-by-card construction and repeated large CPU-rotated redraws without
reparenting or adding another rendering path.
