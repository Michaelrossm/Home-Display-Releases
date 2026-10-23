# Dateien und Hintergrundbilder

Die fest integrierte App **Dateien** bietet Seitenleiste, Raster-/Listenansicht,
Ordnernavigation, seitenweise Dateisuche und Bildvorschau. Lokale Orte sind
Display-Daten, interne App-Pakete und SD-Karte. Sie verändert oder löscht keine
ausgewählten Originaldateien und führt keine Dateien aus. Zentrale Systemdaten
und Zugangsdaten sind nicht als Explorer-Ort verfügbar.

NAS-Zugriff ist standardmäßig ausgeschaltet. Nach Freigabe durch den bestehenden
Einstellungs-PIN zeigt der Explorer aktivierte SMB2/3-Profile des zentralen
NAS-Dienstes. NAS-Dateien können ohne Überschreiben vorhandener Dateien auf SD
kopiert werden. Für Vorschau und Hintergrundauswahl zuerst diese lokale Kopie
öffnen. WebDAV-Ordner werden hier nicht angeboten.

Unter **Einstellungen → Anzeige** lassen sich Hintergrundbilder aktivieren,
über den Explorer auswählen und abdunkeln. Der Import erstellt eine zugeschnittene
RGB565-Kopie mit Prüfsumme. Zwei verwaltete Cache-Dateien erlauben den Austausch,
ohne den aktiven Hintergrund vorzeitig zu überschreiben. Die Originaldatei bleibt
unverändert. Ohne SD wird die Kopie auf 640×400 begrenzt; mit SD auf die UI-Auflösung.

Unterstützt: PNG, JPEG, BMP, GIF (erstes Bild), statisches WebP und TGA.
JPEG-EXIF-Ausrichtung wird berücksichtigt. Keine HEIC/HEIF-, AVIF- oder SVG-Unterstützung.
Dateigrenze 4 MiB; reguläre Decoder höchstens 4 Megapixel. Auf dem P4 versucht der
skalierende JPEG-Decoder größere JPEGs bis 64 Megapixel zu verkleinern; nicht
unterstützte Varianten werden sicher abgewiesen. Lua-Paketbilder behalten die
engeren Grenzen aus `lua-images-and-emoji.md`.

Dateizugriff und Decodierung laufen außerhalb des UI-Locks im bestehenden Worker.
Keine zusätzliche LVGL-Instanz, Displayinitialisierung oder App-Task.
Host-Tests prüfen die tatsächlichen portablen Decoder, inklusive progressivem JPEG
und EXIF-Drehung. NAS, skalierter P4-JPEG-Decoder und Darstellung erfordern zusätzlich
einen Gerätetest; ein erfolgreicher Build ersetzt diesen nicht.
