# Aktueller Prüfstand

Dieser Eintrag ersetzt die verstreuten historischen VALIDATION-/BUILD_VALIDATION-
Schnappschüsse. Alte Prüfzahlen sind keine Aussage über den aktuellen Stand.

## Verbindliche Prüfungen

```powershell
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py validate
python tools/dev.py build --env home_display_p4_legacy_dev
```

Abschluss am 2026-10-17:

- Doctor: erfolgreich.
- Gesamttests: 406 Tests plus vier Architekturprüfungen erfolgreich.
- Zwei ergänzte Bereinigungs-/Dokumentationstests separat erfolgreich.
- Validator: erneut alle 408 Tests erfolgreich, `VALID`.
- Vollständiger Legacy-360-MHz-Build und abschließender inkrementeller Build: SUCCESS.
- PlatformIO: statisches RAM 56.440 Bytes, Flash 6.197.704 Bytes (9-MiB-OTA-Slot).
- `git diff --check`: erfolgreich.
- 45 obsolete Dateien entfernt; über Git wiederherstellbar. Persönliche Daten,
  lokale App-Pakete und Build-Caches nicht gelöscht oder hochgeladen.
- Decoder: flächengewichtetes PNG-Downsampling, bilineare RGB565-Nachskalierung,
  unveränderte Pixel bei reiner Drehung, Kantenglättung im ruhenden Vollbild.
  Echte PNG/BMP-Streifen- und JPEG-EXIF-Hosttests erfolgreich.

Host-PNG-Beispiel (1600x1000 auf 800x500): erfasste Decoder-Heap-Spitze
881.082 statt 8.000.000 Bytes im bisherigen BGRA-Pfad. Keine P4-Geschwindigkeits-
oder Stromverbrauchsmessung. Keine zusätzlichen dauerhaften Tasks.

## Nicht damit nachgewiesen

Keine Hardwareabnahme und keine Produktionsfreigabe. Insbesondere tatsächliche
Bildqualität, Zoom-Latenz, Touch/Wake, Panel-Flackern, Netzwerkstabilität und
Leistungsaufnahme müssen am passenden P4 geprüft werden.
Keine Partitionen geändert und kein automatisches Formatieren oder Flashen.
Produktionsvalidierung erfordert einen echten Signaturschlüssel.

Funktionsbezogene Messungen:
[Bilder](photos-performance.md), [Rendering](adaptive-widget-rendering.md).
