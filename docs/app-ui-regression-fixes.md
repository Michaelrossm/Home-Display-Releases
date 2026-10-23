# Local app UI corrections

Calculator 0.1.4 builds its UI in a coroutine resumed only by on_tick.
Construction yields between individual native bridge calls after 3 ms or
12 calls, rather than measuring only after an entire button. No task or
firmware time-budget change is introduced. Decimal arithmetic is retained.
The runtime test models 2 ms per native construction call and checks the
12 ms callback boundary and complete construction of all 24 buttons.

Printer panel 0.1.3 separates the temperature title from its sensor status
and separates camera status from the protocol subtitle. Geometry tests
check these sibling text regions as well as existing card boundaries.

These are host-level checks, not visual hardware validation. A single
native bridge call cannot be preempted by Lua. Installable app sources
and versioned ZIP packages remain local and excluded from Git.
