# Drucker-App und gemeinsame Firmware-Schnittstelle

Installierbare Drucker-Apps werden außerhalb dieses Firmware-Repositories
verwaltet. Lokale Pakete unter `apps/` oder `dist/` sind keine veröffentlichten
Firmwarebestandteile. Neue Pakete verwenden Schema 3 und getrennte
`main.lua`/`settings.lua`; siehe [Paketstandard](LUA_APP_PACKAGES.md).

## Gemeinsame Dienste

Die App nutzt `PrinterService`, dessen Adapter, zentrale Netzwerkjobs, Storage
und Bildressourcen. Keine direkten Netzwerkzugriffe, eigenen Decoder oder
permanenten App-Tasks. Profile und Zugangsdaten bleiben ownergebunden.

- RRF/DSF, Moonraker/Klipper und OctoPrint/Marlin verwenden ihre eigenen Adapter.
- Status und Diagramme verwenden gecachte Daten; Steuerbefehle unterliegen
  nativen Prüfungen und werden bei Timeout nicht blind wiederholt.
- Dateien/Makros werden begrenzt und seitenweise geladen, nicht als G-Code-Inhalt
  für eine Dateiliste heruntergeladen.
- Druckeraktionen: [typisierte Steuerung](printer-typed-controls.md).
- Druckfarm/Warteschlange: [Sicherheit](PRINTER_FARM_QUEUE_SAFETY.md).

## Miniaturen

`hp_printer_preview(profile,{path=...,maxWidth=48,maxHeight=48})` fragt ein kleines
Thumbnail an. Große Ansichten fordern ihre eigene Größe an (Standard 320x220,
höchstens 512 je Kante). RRF und Moonraker wählen die kleinste ausreichende
verfügbare Quelle; lokale Slicer-Kommentare verwenden dieselbe begrenzte Auswahl.
64er und 300er Slicerbilder werden dadurch sinnvoll unterschieden.

Der gemeinsame `ImageCodec` erzeugt das begrenzte RGB565-Bild.
Keine STL-/G-Code-Vollübertragung oder neuer Renderer.
Siehe [adaptive Widgets und Vorschauen](adaptive-widget-rendering.md) und
[Medien-API](MEDIA_STORAGE_API.md).

## Paketprüfung

`tools/package_printer.py` ist ein optionales Werkzeug für externe/lokale
Paketquellen. Es prüft beide Lua-Einstiege mit dem gepinnten Parser.
Der Firmware-Installer prüft Manifest, Berechtigungen, Speichergrenzen,
vollständige Lesevorgänge und Rollback selbst. Ein App-ZIP ersetzt keine
notwendige Firmwareaktualisierung.

Hosttests sind keine Drucker-Hardwareabnahme. Ohne ausdrücklichen Auftrag keine
Bewegungs-, Heiz-, Druck- oder Kalibrierbefehle an ein reales Gerät senden.
