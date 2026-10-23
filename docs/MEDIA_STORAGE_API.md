# Generic Lua media/storage API

Firmware foundation only: no photo app, favorites or slideshow profiles. Existing
APIs/schema number/themes/display drivers are preserved. All file/SMB/decode work
uses the existing NetworkWorker. LVGL mutations use the existing locked UI pass.
No new permanent task or polling timer is created.

## Permissions and paths

Schema 2 adds `storage.read`, `storage.nasRead`, `system.wallpaper`,
`power.idleIntegration`. `storage.writeAppData` aliases existing `storage`;
declaring both aliases is rejected. Native and both web installers accept these
names. Schema 1 does not gain them automatically.

`storage.read` exposes public SD files, not hidden/private system data, credentials
or app packages. Public `/homepanel/media/...` is readable. NAS uses enabled
central SMB profiles without returning their host/user/password.
Paths: `sd:/Bilder/`, `storage://sd/Bilder/`, `nas123:/Fotos/`,
`storage://nas123/Fotos/`. NAS number = stable profile ID, not a list index.

## Jobs

Async calls return job ID or `nil` when the shared four-slot queue is full.
Completion arrives through `on_event(name,{job=id})`. Read `hp_job_result(id)`:
`done`, `ok`, `error`, `body`. JSON results use `hp_json_decode(body)`.
Keep IDs and use the existing status fallback if events are dropped.
Results/cancellation are owner-scoped and responses limited to 16 KiB.

## Storage

- `storage.providers()`: array `{id,name,online,root}`; NAS also `known` (a real
  connection attempt occurred). Network online alone does not prove NAS reachable.
- `storage.list(path,{files=true,directories=true,extensions={"jpg","png"},
  sort="name",offset=0,limit=100})`: `{entries={{name,path,size,date,directory}},
  more,nextOffset}`. Follow `nextOffset`, not assumed page length.
- `storage.stat(path)` / `storage.exists(path)`: both async, result
  `{exists,size,date,directory}`. Offline/provider errors can yield `exists=false`;
  that is not proof of deletion.
- `storage.read(path)` / `storage.open(path)`: bounded bytes, not streaming
  file handles. Maximum 16 KiB; image APIs handle larger pictures.

List limits: 1..100 entries, eight ASCII extensions, names ≤96 bytes, offsets
≤1,000,000. Name sorting ascending UTF-8 byte order; date sorting newest first,
name tie-break. A bounded sorted window scans again for late pages; 15-second
scan deadline. SMB uses raw 8-KiB query pages instead of materializing a complete
directory with `opendir()`. Hidden/invalid/overlong names are omitted, not truncated.

## Images

`image.loadAsync(path,{maxWidth=1280,maxHeight=800,fit="contain"})` ->
`image.ready` and JSON `{handle,width,height}`. Dimensions 1..1280; `cover`
fills/crops, `contain` preserves aspect without upscaling, `center` crops at
native size. Bilinear resampling; no selectable quality, blur or HEIC decoder.

For opaque photos/previews use
`image.loadAsync(path,{maxWidth=800,maxHeight=500,fit="contain",opaque=true})`.
The existing shared `ImageCodec` then provides bounded RGB565 output, reduced
JPEG/WebP decode and row-streamed non-interlaced PNG. Transparent pixels are
composited against black; do not enable this for alpha-sensitive icons. Only
`contain` is supported with `opaque=true`; wallpaper saving and cover/center
retain their existing paths. The option also applies to `thumbnailAsync` with
an options table. Existing calls remain unchanged. Resolution limits, owner
isolation, worker lanes and resource budgets are unchanged.

Native consumers use the same functions: Photos selects its configured render
edge and bounded prefetch cache; printer previews request 320x220 RGB565 with a
64-KiB encoded/512x512 source limit; Files requests 580x470 RGB565. Desktop
wallpapers use the centrally persisted RGB565 `.hpi` copy and keep one displayed
image. These are consumer policies, not separate decoders or new shared caches.
ARGB widget/icon loads remain alpha-preserving. No permanent task was added.

`image.thumbnailAsync(path,220,150)` or `(path,options)` uses dimensions 1..512.
SD cache hashes URI/date/size/dimensions/fit in StorageService's private user-data
cache. Limit 64 files/8 MiB, filesystem-resolution LRU timestamps, only managed
SHA256 `.hpi` files evicted. Corrupt entries are rebuilt. No SD -> no disk cache.

`image.create(parent,handle)` pins a native descriptor; `image.setSource(widget,
handle)` reuses that widget. Only owned widgets created by this API are accepted.
`image.release(handle)` invalidates the handle, not a still-pinned widget buffer.
Owner cleanup on close/disable/evict/uninstall cancels jobs/releases resources.
Acquisition/store may fail during contention; resource release is serialized
through the existing metadata lock and does not silently lose a handle.
`image.cancel(job)` delegates to existing owner-scoped cancellation.

Limits: 32 resource slots, 8 MiB retained pixel buffers; encoded input ≤4 MiB.
JPEG ≤64 MP subject to scaled-decoder limits; PNG/BMP/WebP/GIF/TGA ≤4 MP.
EXIF orientation is applied. Native-size `center` uses the 4-MP decode limit.
GIF first frame only. Transient encoded/decoder scratch buffers are additional
to the retained pool, bounded by input/dimension limits; allocation failure is
reported. Cancellation occurs during reads, after vendor decode and every
resampling row. Vendor decode itself is not safely interruptible.

```lua
local job=image.loadAsync("storage://sd/Bilder/foto.jpg",{fit="contain"})
function on_event(name,event)
  if name~="image.ready" or event.job~=job then return end
  local result=hp_job_result(job)
  if not result or not result.done or not result.ok then return end
  local info=hp_json_decode(result.body)
  local widget=image.create(hp_root(),info.handle)
  if widget then image.release(info.handle) end
end
```

## Wallpaper / power

- `system.wallpaper.set(path,{mode="cover",dim=0.15})`: async, central PIN
  authorization, optimized local atomic inactive-slot copy, including NAS sources.
- `system.wallpaper.get()`: `{enabled,path,mode,dim}` from central settings.
- `system.wallpaper.reset()`: async, theme fallback; original files not deleted.
- `system.wallpaper.preview(path,options)`: foreground-only native cancel/set
  overlay, returns load job. Set queues worker save; app switch cancels preview.
  PIN is rechecked on apply. No timer/task. Failure keeps an error visible.

Settings → Anzeige: file picker/preview, next-import fit, 0..100% dim, reset.
A cropped optimized copy cannot reconstruct its original; change fit then import
again. Homescreen retains the local image across normal app switches/invalidations.

`hp_open_app(arguments)` / `app.launch("own-id",arguments)` queue own-app
navigation. Trusted native callers use `AppManager::openWithArguments`.
`app.arguments()` exposes an object ≤256 bytes/depth 4 in `on_open`; optional
`on_launch` notifies after parameterized open/resume. States remain separate.

`power.registerIdleAction({after=300,enabled=true,
arguments={mode="slideshow",profile=1}})` asynchronously saves one owner-scoped
inactivity action under settings PIN protection. Delay 10..86400 seconds.
Settings → Energiesparen selects an eligible enabled app and 1..60-minute delay,
preserving app-supplied arguments. Start only from Home, never over another app;
disabled/uninstalled owners cannot launch. Touch ends the action/returns Home.
It keeps the display active; existing dim/saver/off policies remain otherwise.

Events: `storage.mounted/unmounted`, `theme.changed`, `power.wake/sleep`;
network broadcasts `storage.providerOnline/Offline`. Real NAS attempts also emit
`storage.providerOnline.nas123` / `storage.providerOffline.nas123`. No permanent
NAS reachability probing. Background callbacks may submit bounded storage/image
jobs; UI and wallpaper mutation remain fenced.

## Verification boundary

Host tests exercise real pool ownership/pinning/budget/cleanup, 550-file sorted
windows, cancellation and malformed/Unicode SMB pages, plus integration checks.
Physical SD removal, SMB recovery, offline NAS wallpaper persistence, visual
EXIF, touch/idle/sleep and screen-switch validation require the real board and
are pending. Compilation is not hardware validation.

Reference: [libsmb2 directory implementation](https://github.com/sahlberg/libsmb2/blob/master/lib/libsmb2.c).
