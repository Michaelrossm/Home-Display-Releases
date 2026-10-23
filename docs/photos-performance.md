# Bilder-Pipeline und Mehrfach-Uploads (lokal, Abschluss 2026-10-17)

## Umsetzung

Der aktuelle lokale Projektstand bleibt Grundlage; vorherige Sprachpaket- und
Vollbildänderungen wurden erhalten. Keine neuen dauerhaften Tasks, Decoder,
Touch-Treiber, Displaytreiber oder Netzwerk-Worker. Kein Flash, Commit oder Push.

- JPEG nutzt den vorhandenen reduzierten Decoder, direkt nach RGB565. WebP
  verwendet die Skalierung desselben vorhandenen Decoders. Dessen bisheriges
  `WEBP_REDUCE_SIZE` schaltete die Skalierung ab und wurde entfernt.
- Nicht-interlaced PNG wird zeilenweise gelesen: eine Quellzeile statt eines
  vollständigen Original-RGBA-Puffers. Interlaced PNG und andere bestehende
  Fallbackformate bleiben durch Originalpixelzahl und Speicherreserve begrenzt.
  WebP-Lossless kann intern weiterhin einen größeren Arbeitspuffer benötigen.
- RGB565 halbiert die fertige Bildfläche gegenüber BGRA. Qualität: längste Kante
  800/1280/1600/1920, Standard 1280, absolut höchstens zwei Millionen Pixel.
  Die vorhandene Konfiguration speichert das zusätzliche Feld `renderEdge`.
- Ein vorhandenes Rasterthumbnail erscheint sofort; JPEG/WebP erhalten nötigenfalls
  eine 480er Vorschau und höchstens eine Qualitätsstufe. Kleine Quellen werden
  nicht doppelt decodiert. PNG wird nicht für eine zweite Voll-Decodierung vorgeladen.
- Höchstens drei Cacheeinträge/6 MiB innerhalb des bestehenden 8-MiB-Ressourcenpools,
  eine komprimierte Quelle bis 4 MiB und ein ausstehender Bildauftrag. Vorladen
  nur für den nächsten Kandidaten; Speicherdruck verwirft zuerst Spekulationen.
  Decoderarbeitspuffer und noch vom Widget gehaltene Altbilder kommen temporär
  hinzu, mit freien Speicherreserven vor teuren Allokationen.
- Neue Auswahl verdrängt alte Aufträge; Decoder erhalten das Abbruchflag.
  Mediengenerationen verhindern die Übernahme nach SD-/USB-Wechsel.
- Bildwechsel und Qualitätswechsel ersetzen nur das Bildobjekt. Die alte Fläche
  bleibt gepinnt, bis die neue gültige Ressource veröffentlicht ist. Alte Widgets
  werden anschließend über LVGL verzögert gelöscht. Ressourcenfreigabe verliert
  keine Handles mehr durch eine kurzzeitig belegte Metadaten-Sperre.
- Pinch 1–4x um den Berührungsmittelpunkt, Einfinger-Verschieben und Randbegrenzung
  benutzen LVGL-Transformationen ohne Decodierung je Geste. Updates nur bei
  Änderungen; adaptive Begrenzung und Qualitätswechsel nach Gestenpause.
- Vollbild/Bilderrahmen nutzen zentrale Idle-/Performance-Leases; Home, Schließen,
  Fehler und Sicherheitssperre geben diese frei. Manuelle und Sicherheitsregeln
  werden nicht übergangen. Der Bilderrahmen behält Vollbild und Bedienlogik;
  Bilddauer bleibt am Display und im Web unabhängig von Bibliotheksoptionen einstellbar.
- Löschen erst nach Bestätigung, nur SD/USB unter `/Bilder/`, mit Pfad-, Rechte-,
  PIN- und Medienprüfung im vorhandenen Worker. NAS bleibt lesend. Kein Papierkorb.
  Erfolgreiches Löschen aktualisiert Bibliothek, Favoriten und Cache.

## Gemeinsame Upload-Infrastruktur

`hpWeb.upload()` verwendet dieselbe begrenzte Browser-Anfragequeue wie die
vorhandenen Zugriffe; `hpWeb.batch()` führt jeweils einen Stapel sequenziell aus.
Keine automatischen Wiederholungen schreibender Requests.

- Bilder: Auswahl/Drag-and-drop bis 128 Dateien, bestehender atomarer Uploadendpunkt,
  je Datei höchstens 4 MiB, Einzel-/Gesamtfortschritt, Abbruch und Fehlerfortsetzung.
  Freier Speicher wird vorab geprüft; der Server bleibt maßgeblich. Nur die gerade
  übertragene Datei wird gestreamt, kein Gesamtstapel im ESP32-RAM.
- Apps: bis 32 ZIPs, je höchstens 8 MiB, jeweils nur ein entpacktes Paket im Browser.
  Bestehende Schema-/Datei-/Versionsprüfung und Installer/Commit/Rollback bleiben.
  Downgrades benötigen Bestätigung. Eine befristete, ownergebundene Staging-Lease
  verhindert, dass moderne Browser gegenseitig Dateien einer Installation mischen.
  Alte externe Clients ohne Sessionheader bleiben kompatibel, sind untereinander
  jedoch nicht individuell unterscheidbar.
- Fehler einzelner Dateien/Pakete lassen andere weiterlaufen; Authentifizierungsfehler
  stoppen. Verdeckte/inaktive Tabs beginnen keine weiteren Dateien. Abbruch startet
  nichts neu. Ein bereits abgesendeter App-Commit ist nicht sicher unterbrechbar;
  unbekannte Timeout-Ergebnisse müssen geprüft werden, nicht blind wiederholt.

## Messungen und Nachweisgrenzen

Lokaler Ausgangsbuild: statisches RAM 56.440 Bytes, Flash 6.136.540 Bytes.
Zwischenstand vor der weiteren Integration, `home_display_p4_legacy_dev` (360 MHz): SUCCESS, statisches RAM
56.440 Bytes (Differenz 0), Flash 6.182.122 Bytes (Differenz +45.582).
Dies ist PlatformIOs statische RAM-Auswertung, keine Aussage über dynamische
Heap-/PSRAM-Spitzen. Zusätzliche dauerhafte Tasks: 0.

Realer Host-Decodertest mit den projektgebundenen libpng-/libwebp-Quellen,
1600x1000 RGBA-PNG auf 800x500:

| Erfasste Größe | bisheriger BGRA-Pfad | neuer PNG/RGB565-Pfad |
| --- | ---: | ---: |
| Decoder-Heap-Spitze | 8.000.000 Bytes | 881.082 Bytes |
| Einzelmessung Host-Laufzeit (Qualitätskorrektur) | 29.376 µs | 25.864 µs |

Das sind PC-Werte, keine ESP32-Zeiten und keine allgemeine Geschwindigkeitsgarantie.
Die PNG-Qualitätskorrektur verwendet flächengewichtetes Downsampling statt
Nearest-Neighbor. Zusätzlich zur Quellzeile werden eine kleine gefilterte Zeile
und RGB-Akkumulatoren gehalten (15 Bytes je Ausgabespalte). Der andere RGB565-
Nachskalierungspfad interpoliert bilinear; JPEG/WebP nutzen weiter ihre direkte
Decoderskalierung. Im ruhenden Vollbild ist LVGL-Kantenglättung aktiv, während
aktiver Gesten vorübergehend aus und nach 250 ms Pause wieder an. Kein erneutes
Decodieren je Fingerbewegung. Ein Streifentest prüft PNG und BMP auf korrekte
Grauwertmittelung statt Schwarz-/Weiß-Aliasing. RGB565 hat weiterhin weniger
Farbstufen als ARGB8888; Zoom über die Renderauflösung kann keine Details erfinden.
Die Heapmessung erfasst die verwendeten Heap-Hooks, nicht jeden C-Library-Puffer.
Aktueller finaler Build nach Qualitätskorrektur/Bereinigung: siehe [Prüfstand](VALIDATION.md).
Pixel/Alpha, WebP-Verkleinerung, JPEG-EXIF, Fehler, Abbruch und Allokationsfehler
werden mit echten Decodern geprüft; Cache/Medienwechsel/Zoom/Uploads mit Hostfakes.

Laufzeitdiagnostik `imageMetrics`: Queue-, Lese-, Decode-, erste/finale
Bereitstellungszeit, Cachetreffer, Abbrüche, Fehler, Speichergrenzen und freier
Heap/PSRAM. Bereitstellung ist nicht der physisch sichtbare Panelzeitpunkt.
Bestehende LVGL-Frametime-Diagnostik bleibt maßgeblich für Renderkosten.

Nicht am Gerät gemessen: Erstbild-/Wechselzeit, Pinch-Latenz, thermisches Verhalten,
physisches Hot-Unplug, NAS-Durchsatz und türkisfarbenes Aufblitzen. Die konkrete
physische Blinkursache ist nicht bestätigt. Gepinnte Puffer/atomarer Austausch
härten einen plausiblen Fehlerpfad; Displayrotation/Flushkonfiguration wurde nicht
auf Verdacht geändert. Kein zusätzlicher PPA-Pfad wurde eingeführt.

## Geänderte Bereiche dieses Auftrags

- `components/home_image_codecs/CMakeLists.txt`
- `src/apps/photos/PhotoApp.cpp`, `PhotoZoom.h`
- `src/hardware/DisplayManager.{h,cpp}`
- `src/services/ImageCodec.{h,cpp}`, `ImageOrientation.h`, `ImageRenderPolicy.h`
- `src/services/ImageResourceService.{h,cpp}`, `PhotoImageCache.{h,cpp}`
- `src/services/FileBrowserService.{h,cpp}`, `StorageService.{h,cpp}`
- `src/services/PhotoLibraryService.{h,cpp}`, `PhotoWebApi.h`, `PhotoWebPage.h`
- `src/services/IdleService.h`, `AppWebLifecycleScript.h`, `InstallerTransferLease.h`
- `src/services/FileTransferService.cpp`, `WebServerService.cpp`
- `tests/test_photo_app.py`, `test_photo_viewer.py`, `test_photo_performance.py`,
  `test_photo_deletion.py`, `test_upload_batches.py`, `test_image_codec_native.py`
- `tests/native/image_codec_heap.h`, `image_codec_host.cpp`,
  `image_codecs/CMakeLists.txt`

Andere bereits lokale Änderungen, insbesondere Sprachpakete, gehören nicht zu
dieser Auflistung und wurden nicht verworfen.

## Abschlussprüfungen

- Ausgangsstand: Doctor erfolgreich, 387 Tests und Legacy-Build erfolgreich.
- `python tools/dev.py test`: 401 Tests erfolgreich; vier zusätzliche
  Architekturprüfungen erfolgreich.
- `python tools/dev.py validate`: VALID, einschließlich erneuter 401 Tests.
- `python tools/dev.py build --env home_display_p4_legacy_dev`: SUCCESS.
- `git diff --check`: erfolgreich (nur Hinweise zu Windows-Zeilenenden).
- Zwei zunächst fehlgeschlagene Bestandstests waren veraltete Testhilfen:
  JavaScript-Variablenausschnitt und fehlende Heap-Abfragen im Decoder-Fake.
  Aktualisiert, ohne die Manifestprüfungen abzuschwächen.
- Der neue echte Decoder-Test fand zusätzlich die abgeschaltete WebP-Skalierung;
  nach Korrektur sind Verkleinerung, Pixel/Alpha, EXIF und Fehlerpfade grün.
- R3/400 MHz nicht gebaut; für das bekannte Legacy-Gerät nicht erforderlich.
- Keine Hardwarevalidierung, kein automatischer Flash, keine GitHub-Änderungen.

Weitere angepasste Testhilfen: `tests/test_desktop_schema3.py` und
`tests/native/image_codecs/esp_heap_caps.h`.

## Gemeinsame Nutzung geprüft (2026-10-17)

Die Decoderoptimierungen lagen bereits zentral in `ImageCodec`, nicht in der
Bilder-App. Ergänzt wurde lediglich die Nutzung: Druckervorschauen (320x220)
und Dateivorschau (580x470) fordern RGB565 an. `imageNow` reicht die Auswahl
ebenso wie der asynchrone Dateidienst an denselben Decoder durch. Lua erhält
das rückwärtskompatible Opt-in `opaque=true` für contain-Bilder/Thumbnails.
Transparenz wird dabei gegen Schwarz zusammengesetzt; Icons und cover/center
bleiben unverändert. Desktop-Hintergründe waren bereits RGB565-`.hpi`-Kopien.
Keine zusätzliche Cache-/Decoderimplementierung und keine neuen Tasks.

Der nachfolgende vollständige Legacy-Build meldet RAM 56.440 Bytes und Flash
6.191.716 Bytes; darin ist auch die neu kompilierte WebP-Komponente mit aktiver
Skalierung enthalten. Verbraucherparameter und Decoder-Ausgabe werden zusätzlich
in `test_image_codec_native`, `native/image_codecs/check.cpp` und dem vorhandenen
Drucker-Service-Test geprüft. API: `docs/MEDIA_STORAGE_API.md`.
