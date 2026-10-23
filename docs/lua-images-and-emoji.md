# Lua-Bilder und Emoji

Alle normalen UI-Schriftgrößen enthalten Umlaute, die bisherigen LVGL-Symbole
und häufige **einfarbige** Emojis (z. B. 📻 🔊 📅 ⚙ 🏠 ✅). Bestehende Labels
benötigen keine Änderung. Nicht enthaltene Unicode-Zeichen können weiterhin
Platzhalter zeigen. Farbige Emojis, Flaggen, Hautfarben und ZWJ-Ligaturen werden
nicht vollständig unterstützt: Präsentationsselektoren/Hautfarben sind unsichtbar,
zusammengesetzte Emojis erscheinen als einzelne Symbole statt als Ligatur.

Die Emoji-Glyphen stammen aus Noto Emoji (SIL Open Font License 1.1),
Quelle: https://github.com/google/fonts/tree/main/ofl/notoemoji .
Font und Lizenz liegen unter `tools/assets/`; `python tools/generate_ui_fonts.py`
erzeugt die sechs statischen Schriften. Keine Laufzeit-Schriftallocation.
SHA256 NotoEmoji.ttf: `de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551`.

## Bilder aus dem App-Paket

```lua
local image = hp_ui_image(root, "assets/logo.png")
-- image kann nil sein (ungültiger Pfad, Speicherlimit, belegte Worker-Queue).
-- Ansonsten asynchron: "loading", "ready" oder "error".
local status = image and hp_ui_image_status(image)
```

ZIP-Struktur: `app.json`, `main.lua`, optional `settings.lua`, `assets/logo.png`.
Web-Installer, Update, Backup und Rollback übernehmen Bild-Assets mit.
PNG, JPEG, BMP, GIF (erstes Bild), statisches WebP und TGA, bis 256×256 Pixel
und 128 KiB je Datei; höchstens 16 Dateien bzw.
512 KiB Bilddateien je Paket. Dateinamen: Buchstaben, Zahlen, `_`, `-`, `.`;
keine weiteren Unterordner, absoluten Pfade, URLs oder `..`. Die App kann nur
aus ihrem eigenen Paket lesen. Decoderfehler ergeben `error`, nicht einen
direkten Zugriff auf fremde Dateien. Keine Animationen, SVG, HEIC/HEIF oder AVIF.
Nicht benötigte Metadaten (u. a. komprimierte Text-/ICC-Blöcke) werden vor dem
Dekodieren entfernt; Alpha und die begrenzten Gamma-/sRGB-Informationen bleiben.

Laden und Bild-Decodierung laufen im vorhandenen NetworkWorker über StorageService.
Nur das Einsetzen des fertig dekodierten PSRAM-Bildes läuft unter der vorhandenen
UI-Synchronisierung. Schließen einer App verwirft noch laufende Bildaufträge sicher.
Maximal 512 KiB native Bild-/Bridge-Ressourcen je Lua-State; die Reservierung wird
nach dem Laden auf die tatsächliche Pixelgröße reduziert und beim Löschen freigegeben.
Der bisherige Aufruf `hp_ui_image(root, {width=..., height=..., pixels=...})`
für RGB565-Daten bis 128×128 bleibt unverändert verfügbar.
