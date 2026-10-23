# Performance / memory patch — v0.2.5

## Calendar memory policy

No full calendar database is loaded into RAM.

Static RAM:
- max 16 PersonRecord
- max 32 CalendarRecord
- max 32 UpcomingEvent cache

Events stay on SD/internal fallback and are scanned sequentially.

Participant membership uses a 16-bit mask instead of arrays of copied e-mail
addresses or duplicated event records.

Birthdays are virtual, so they cost no EventRecord storage.

## Shared worker

CalendarService and CalendarSyncService join the existing BackgroundScheduler.
No extra FreeRTOS task is created for calendar/sync.

The shared worker stack was reduced from 10 KiB to 8 KiB.

## Event bus

Fixed handler capacity reduced from 20 to 16 after the architecture review.
No heap allocation occurs while publishing.

## Files

Binary fixed-size records avoid a large JSON parser/document for the entire family
calendar.

Atomic writes are available through StorageService for compact metadata tables.

## Power

Calendar refresh runs only every 5 minutes in the shared worker and uses a short
PerformanceGuard while scanning. It does not keep the CPU at full speed between
updates.
