# UI storage / performance — v0.2.7

- No per-app bitmap icon packs.
- Desktop symbols come from the existing LVGL font.
- Only one optional wallpaper is active.
- Image cache remains intentionally small.
- JPG decoding is wrapped in a short PerformanceGuard.
- App roots are transparent and reuse the central shell/background.
- Wallpaper file lives on SD, not internal flash.
- Missing wallpaper never blocks boot.
- LVGL filesystem bridge is read-only for wallpaper access.
- The bridge reuses SD_MMC already owned by StorageService; it does not mount a
  second SD filesystem.
