# Native app browser interfaces

The existing authenticated HTTP server on port 80 serves `/calendar` and
`/files`. No additional server or permanent task is started for either app.
These routes are already advertised by the native apps in the app overview.

## Calendar

The agenda reads the central local calendar cache with a bounded date range,
32-event pages, stable cursors, calendar and user filters. It never waits for
cloud synchronization to finish. Pending/incomplete cloud results are labelled.
The event editor supports local event creation, optimistic-revision updates,
single-occurrence/whole-series deletion, participants, full address, telephone,
notes, recurrence and reminders. Provider HTML is displayed as text, never
executed. Existing read-only calendars and virtual events remain read-only.
Existing calendar/person/account/holiday/synchronization configuration remains.

All operations use CalendarApi through the existing LuaJobService/NetworkWorker.
Admin authentication, CSRF, tablet lock and settings PIN guards remain required.
Browser reads use the shared visibility lifecycle; no polling occurs while the
page is hidden. The bounded submitted backend job may finish after the browser
stops waiting; writes must not be automatically retried after a timeout.

## Files

Existing FileTransferService handlers are reused on both port 80 and the
optional administrative server. Mounted internal flash, AppFS, SD and USB are
listed in pages of 200 entries, with bounded page offsets. User files support
download, upload, directory creation, rename and confirmed deletion. Filenames
are escaped in markup/URLs and never injected as JavaScript source.

New transfers use 4 KiB buffers, a 32 MiB upload limit and a 120-second transfer
deadline. Uploads commit from unique sibling temporary files; existing files are
not overwritten. Failed uploads remove only their own temporary file. Both
frontends serialize file mutations with a non-blocking mutex and notify the
central storage revision on successful mutation. USB uses the existing
generation-bound filesystem, not a second driver. No UI lock is acquired.

AppFS is read-only here; installation/removal belongs to the existing installer.
Protected flash namespaces cannot be downloaded or modified. Empty uploads are
valid. HTTP transfers execute on the existing server task, not LVGL. A running
transfer can occupy this single HTTP server until completion/deadline; this is
not claimed to be an asynchronous transfer-worker redesign.

## Remaining scope limits

Cloud event editing is not added because the existing provider API is read-only.
NAS write operations, cross-volume move/copy, browser media preview and import/
export are not implemented by these new routes. Do not treat this as a complete
replacement for a desktop file manager or a cloud calendar provider UI.
Hardware/browser integration still requires flashing the built firmware and
testing against mounted media and configured accounts.
