# Modern UI design — v0.2.7

## Desktop

The HomePanel shell now provides one central visual system:

- dark blue/graphite gradient fallback background
- optional SD wallpaper
- translucent top/status bar
- glass-like application cards
- rounded icon tiles
- consistent typography
- large touch targets for a 10.1" wall panel

Apps do not define their own desktop icons.

## Icons

HomePanel uses LVGL's built-in FontAwesome-derived symbol set to avoid shipping one
bitmap per button.

Examples:
- Settings: `LV_SYMBOL_SETTINGS` (gear)
- Home: `LV_SYMBOL_HOME`
- Radio: `LV_SYMBOL_AUDIO`
- Power tracker: `LV_SYMBOL_CHARGE`
- Calendar: `LV_SYMBOL_LIST`
- Network: `LV_SYMBOL_WIFI` / `LV_SYMBOL_DRIVE`
- SD: `LV_SYMBOL_SD_CARD`

This is much smaller than separate PNG assets and scales cleanly.

## Wallpaper

Default path:

`/homepanel/media/wallpapers/home.jpg`

Wallpaper is optional.

If:
- no SD is present,
- the file is missing,
- JPG decoding fails,

the desktop automatically shows the built-in gradient.

### Recommended wallpaper

- 1280x800
- JPG
- quality 80–88
- moderate file size

The wallpaper receives a configurable black scrim so text/cards remain readable.

## Image decoder

v0.2.7 enables LVGL's TinyJPEG/TJPGD decoder.

PNG remains disabled by default because full-size PNG decoding has a significantly
larger RAM cost. It can be added later behind a compile-time feature if needed.

## Screensaver

The screensaver now uses:
- large clock
- date
- glass media panel
- proper Previous/Play/Next/Volume symbol buttons

It remains a system feature and still controls any app exposing MediaSessionService.
