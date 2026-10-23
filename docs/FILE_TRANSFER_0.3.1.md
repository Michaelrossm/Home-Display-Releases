# Drag-and-drop file transfer — v0.3.1

Home-Display exposes a small LAN upload page on port 8088.

Example:
`http://home-display.local:8088/`

Files are streamed directly to SD in 4 KiB chunks:
- audio -> `/homepanel/data/imports/music/`
- images -> `/homepanel/data/imports/images/`
- ICS -> `/homepanel/data/imports/calendar/`
- others -> `/homepanel/data/imports/files/`

Upload writes to `.part` first and renames only after a complete transfer.
An interrupted upload therefore cannot replace a prior complete file.

The full file is never buffered in RAM.

Before exposing this beyond a trusted LAN, the upload endpoint should be integrated
with the local admin/session authentication layer.
