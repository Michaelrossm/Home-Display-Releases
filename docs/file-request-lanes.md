# Bounded file work on the existing worker infrastructure

Native Lua storage list/stat/read/range/image operations now select a trusted
Request::Lane in LuaMediaStorageBridge. SD/USB/internal operations use
NetworkWorker::submitLocalTransfer; normalized NAS operations use submitTransfer.
This matches the native file browser and keeps long scans/decodes out of the
short network/status queue. Lua cannot set this dispatch hint. Existing owners,
permissions, cancellation flags, job slots, cache revisions, result bounds and
completion events are unchanged. No additional task or queue is allocated.

The existing transfer task still serializes long work. A synchronous request
already in progress cannot be preempted; no guaranteed zero-latency metadata
response is claimed while a long transfer owns that lane.

PrinterService no longer gates explicit directory reads on status.valid.
Directory reads allocate no temporary PrinterStatus buffer. Missing/disabled
profiles and an offline network without cached entries return terminal errors
instead of indefinitely pending results. Priority command reservations and
foreground/background load rules remain intact; writing commands are unchanged.

Package 0.1.12 adds top-positioned console input, background/log tap keyboard
dismissal and one-second reads of the existing console cache. RRF reply collection
remains the existing seqs.reply/rr_reply read path. The Duet API buffers replies
per client only for a limited time; DWC's browser-local sent-command history is
not a universally readable event log. No probing G-code is sent to synthesize it.

Tests cover invalid/missing status with independently readable directory data,
disabled/missing profiles, offline completion, trusted lane selection and
unchanged HTTP dispatch restrictions; actual Lua tests cover console placement,
periodic cached external replies, keyboard dismissal and the prior browser tests.
