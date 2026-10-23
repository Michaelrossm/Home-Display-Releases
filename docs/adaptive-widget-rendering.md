# Adaptive Widget-Aktualisierung und Drucker-Miniaturen

Lokaler Stand, 2026-10-17. Keine Partitionen geändert, kein Flash, kein Commit/Push.

## Bereits vorhanden

- Ein BSP-LVGL-Task, partielle Dirty-Regions und verifizierte CPU-Rotation mit
  Triple-Partial-Puffern. Keine zweite Render-Task und kein PPA-Umbau.
- Schema-3-Keyed-Diff, begrenzte inkrementelle UI-Aufbauarbeit, identische Texte/
  Werte/Diagrammreihen werden bereits überwiegend übersprungen.
- Datenabonnements vergleichen Providerrevisionen, nutzen dieselben zentralen
  Provider und ownergebundene Jobs. Gleiche Daten lösen keinen neuen Callback aus.
- Wettereffekte laufen separat mit ungefähr 12 Hz, gedimmt unter 5 Hz, unsichtbar/
  Saver/Off pausiert. Kameras sind unabhängig auf höchstens 10 Hz begrenzt.
- Der Desktop baut nicht bei jeder Sensoränderung neu auf; Uhr und Hintergrund
  besitzen getrennte Aktualisierungspfade. Bilder haben eigene begrenzte Jobs,
  Cache und Transformationen, keine Decodierung pro Touchbewegung.

## Tatsächliche Ergänzungen

### Gemeinsame Refresh-Obergrenze

`UiPerformanceMonitor` passt unter dem bestehenden UI-Lock ausschließlich den
vorhandenen Display-Refresh-Timer an. Er ruft keinen zusätzlichen Handler auf.

| Zustand | Refresh-Prüfintervall / Obergrenze |
| --- | --- |
| Statisch / normale Datenupdates | 100 ms / höchstens 10 Hz |
| Touch/Scroll/Navigation/LVGL-Übergänge | 33 ms / ungefähr 30 Hz |
| Gedimmt, ohne dringende Bedienung | 200 ms / höchstens 5 Hz |
| Bildschirmschoner | 100 ms / höchstens 10 Hz |
| Display aus | Refresh-Timer pausiert, keine neuen LVGL-Flushes |

Dies sind Obergrenzen, keine erzwungenen Bildraten: Ohne invalide Bereiche zeichnet
LVGL weiterhin **0 Frames**. Ein leerer Refresh-Check ist kein gerenderter Frame.
Die erste Berührung erhöht die Grenze direkt im vorhandenen LVGL-Ereignis;
Pressing hält sie während einer Geste aufrecht. Dringende Shell-Ereignisse
(Klingeln, Erinnerungen, Navigation, ausstehendes Video, Idle-Wechsel) setzen einen
atomaren Hinweis; der bestehende UI-Lauf macht den Refresh bereit. Bei Off wird
auch dieser Hinweis nicht zum Display-Flush. Die bestehende Wake-/Sicherheitslogik
muss zunächst das Display freigeben. Nach Off wird einmal vollständig invalidiert,
damit akkumulierte Änderungen sauber sichtbar werden.

Diagramme ohne neue Werte werden nicht neu gezeichnet. Die bestehenden Diagramm-
Bridges starten keine eigene periodische Diagrammanimation; daher wurde keine
zusätzliche 15-Hz-Timer-Schleife eingeführt. Wetter/Kamera behalten ihre bereits
niedrigeren eigenen Grenzen. Bestehende imperative `on_tick`-Callbacks werden
nicht pauschal unterdrückt; das würde alte Apps funktional verändern.

### Widget-/Daten-Diff

Die allgemeine Lua-Bridge vergleicht lokale Stylewerte, bevor Farbe, Schrift,
Rahmen, Radius oder Größe gesetzt werden. Nur lokale Overrides werden verglichen:
Eine erste explizite Einstellung, die gerade zufällig dem Theme entspricht, wird
trotzdem gespeichert. Identische Textarea-Inhalte werden ebenfalls übersprungen.
Keine zusätzlichen Widgetzustands-Caches oder dynamischen Vergleichspuffer.

Optional für neue Apps:

```lua
data.subscribe('temperatur', {
  source='printer', profile='mein-drucker', on_change='temperatur_geaendert',
  min_interval_ms=500
})
```

Standard 0 erhält bisheriges Verhalten. Erlaubt: 0 oder 100..60000 ms. Mehrere
Wertänderungen werden bis zur Frist auf den neuesten Stand zusammengefasst.
Erster Stand, Fehler, Lade- und Onlinewechsel werden nicht verzögert. Die vorhandene
`data.step`-Verarbeitung übernimmt die Zustellung; keine neue Abfrage, kein neuer
Timer/Task. Die tatsächliche Zustellung hängt von der bestehenden UI-Verarbeitung
ab (typischer Prüfpfad ungefähr 100 ms), ist keine harte Echtzeitgarantie.
Abonnements mit unterschiedlichen Intervallen haben getrennte Callbackpolitik,
aber weiterhin dieselben Provider-Caches und Netzwerkdienste.

### Diagnosen

- `ui_loop_hz`: tatsächlich bearbeitete Shell-Durchläufe (nicht CPU-Task-Ticks).
- `lvgl_render_fps`: tatsächlich gerenderte Refreshes; Off meldet 0.
- `lvgl_refresh_period_ms`: aktuelle Obergrenze, 0 bedeutet pausiert.
- `lvgl_flush_count`, `lvgl_flush_pixels`, `lvgl_flush_max_pixels`: kumulierte
  Flush-Aufrufe und Rechteckflächen aus LVGLs tatsächlichem Flush-Start-Ereignis.
- `ui_widget_changes/skips`: instrumentierte allgemeine Lua-Text-/Style-/Chart-
  Pfade. Keine vollständige Zählung sämtlicher nativer Widgets.
- `*_mean_us` zusätzlich zu Maximum und Histogrammen; `ui_loop_wall_*` misst
  vergangene Zeit innerhalb des UI-Arbeitsabschnitts, **keine exakte CPU-Zeit**.

Zähler/Histogramme sind begrenzt, atomar und ohne Logging im Renderpfad. Der
Messumfang ist bewusst gekennzeichnet. Flushflächen können überlappen; sie sind
kein Maß für eindeutige geänderte Pixel oder die physische LCD-Bildwiederholrate.

DSI liest den vollständigen Scanout-Framebuffer kontinuierlich. Kleine LVGL-
Änderungen können partiell gerendert/rotiert/kopiert werden; daraus folgt nicht,
dass das physische Panel ausschließlich kleine Rechtecke abtastet. Rotation,
DMA-Cache-Reparaturen und BSP-Framebuffer-Synchronisierung bleiben unverändert.

## Größenabhängige Druckervorschau

`hp_printer_preview(profile,{path=...,maxWidth=48,maxHeight=48})` fragt ein kleines
Bild an; eine große Ansicht kann z.B. 320x220 anfordern. Grenzen 1..512 je Kante,
Standard weiterhin 320x220. Ungültige Typen/Grenzen werden im Worker abgewiesen.
RRF und Moonraker wählen aus den Metadaten die kleinste ausreichende Miniatur,
sonst die größte vorhandene. Lokale G-Code-Vorschauen nutzen dieselbe Auswahl bei
der weiterhin begrenzten Headeranalyse. Der gemeinsame `ImageCodec` decodiert
danach nur zur angeforderten Zielgröße. Quelldatei-/Thumbnail-Limits bleiben.
Kleine quadratische Widgets behalten ihre volle angeforderte Kantenlänge;
die zusätzliche Bildschirm-Pixelgrenze gilt nur für die großen Foto-Qualitätsprofile.
Die Vorlade-Speicherabschätzung verwendet diese gemeinsame Größenregel ebenfalls.
OctoPrint ohne unterstützte Thumbnailquelle bleibt ehrlich nicht unterstützt.

Die separat im Downloadordner abgelegte App 0.1.18 fragt für 48er Listenbilder die
kleine Variante an, für Übersicht und Einzelvorschau die jeweilige Widgetgröße.
Bei 64x64/300x300 Slicerbildern wird somit nur für die große Ansicht 300x300 geladen.
Eine fertige 48x48-RGB565-Fläche benötigt rechnerisch 4.608 Bytes gegenüber
140.800 Bytes für 320x220, jeweils ohne Metadaten/Decoder-Arbeitsspeicher.
Die neue ZIP ist nicht installiert und nicht Teil des Firmware-Repositories.

## Dateien

- `src/core/UiRefreshPolicy.h`
- `src/services/ImageRenderPolicy.h`, `PhotoImageCache.cpp`
- `src/services/UiPerformanceMonitor.{h,cpp}`, `DiagnosticsService.cpp`
- `src/hardware/DisplayManager.cpp`, `src/ui/UiShell.cpp`
- `src/apps/lua/LuaBasicUiBridge.cpp`, `LuaUiBridge.cpp`, `LuaDataSubscriptions.h`
- `src/services/PrinterService.cpp`
- `src/services/printer/PrinterAdapter.h`, `PrinterThumbnailPolicy.h`,
  `PrinterFileJobs.{h,cpp}`, `RepRapPrinterAdapter.cpp`, `MoonrakerPrinterAdapter.cpp`
- Tests: `test_widget_update_policy.py`, `test_ui_performance_monitor.py`,
  `test_v31.py`, `test_printer_files.py`, `test_printer_service.py`,
  `test_image_codec_native.py` (gemeinsame Decoder-Anbindung).

## Nachweis und Grenzen

Gezielte Hosttests prüfen tatsächliche lokale Style-Vergleiche (3000 identische
Aufrufe ohne erneute Setter), Refresh-Pausierung/Wake/Touch/Animation, gemessene
Flushfläche, Zusammenfassen neuer Daten ohne Offline-Verzögerung, kleine/große
Thumbnailauswahl und bestehende Druckersicherheit. Lua-App-Dateien werden mit dem
projektgebundenen Lua-Interpreter auf Syntax geprüft.

Keine FPS-, Energie- oder CPU-Prozentverbesserung am P4 gemessen. Noch am Gerät
zu prüfen: statischer Homescreen/Apps, aktive Gesten, Kontrollzentrum, Dimmen/Off/
Wake, Klingel-/Erinnerungspriorität, einzelne kleine Dirty-Regions und maximale
Renderzeiten mit Kalender, Drucker, Wetter, Kamera und Bildern. Kein Test hat
Drucker-Steuerbefehle an ein reales Gerät geschickt.

Legacy-Build `home_display_p4_legacy_dev`: SUCCESS (vollständig plus abschließender
inkrementeller Build). PlatformIO: statisches RAM 56.440 Bytes, Flash 6.195.986 Bytes.
Gegenüber dem unmittelbar vorherigen gemeinsamen Decoderstand: statisches RAM
unverändert, Flash +4.270 Bytes. Das sagt nichts über dynamische Heap-/PSRAM-Spitzen
aus. Zusätzliche dauerhafte Tasks: 0. Bestehende Deprecation-Warnungen sind noch
vorhanden; sie sind keine Buildfehler.
Der spätere Stand mit zusätzlicher Decoder-Qualitätskorrektur und Repository-
Bereinigung ist separat unter [VALIDATION.md](VALIDATION.md) dokumentiert.

Abschlussprüfungen erfolgreich:

- `python tools/dev.py test`: 404 Tests und vier Architekturprüfungen grün.
- Die zwei anschließend ergänzten Style-/Refresh-Tests separat grün.
- `python tools/dev.py validate`: alle 406 Tests grün, `VALID`.
- `python tools/dev.py build --env home_display_p4_legacy_dev`: SUCCESS.
- `git diff --check`: grün.
- App `main.lua` und `settings.lua`: Syntax mit projektgebundenem Lua geprüft.
- ZIP: `C:/Users/micha/Downloads/3D-Drucker-HomeDisplay-v0.1.18-Adaptive-Vorschau.zip`,
  37.307 Bytes, SHA256
  `ED97E5F5195B523332ACF3F0069AA97A9C24AC6466DD66B4E09441E3B403503B`.

Keine Firmware geflasht, keine App installiert, keine GitHub-Änderung. R3/400 MHz
nicht gebaut. Die Hardwareprüfungen oben bleiben offen.
