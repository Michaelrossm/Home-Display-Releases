# Native Musik und unabhängige CO₂-Waage

`music` ist eine fest integrierte native App; Fitz Touch ist weiterhin nur die
Fernsteuerung externer UNDOK-Radios und wird nicht benötigt.

## Quellen und Bedienung

- Gemeinsame Bibliothek aus `/Musik`, `/Music` und den bisherigen Medienordnern
  auf SD/USB. Eingerichtete NAS-Profile werden über dieselben Provider gelesen.
- Weitere Quellordner in den Musik-Einstellungen hinzufügen/entfernen; danach
  bei gestoppter Wiedergabe die Bibliothek aktualisieren.
- Bibliothek, Alben, Favoriten, Wiedergabelisten, Name-/Datum-Sortierung,
  Lautstärke, Zufall und Wiederholen. Ein Album wird unabhängig vom Medium
  angezeigt; die einzelnen Dateien behalten ihre ursprüngliche Quellen-URI.
- Direkte MP3-/PCM-WAV-Internetradio-URLs mit Sendernamen verwalten.
  Kein Spotify-Connect-Empfänger, AAC, HLS oder DRM. Keine Sender-Volltextsuche.
- Unter Einstellungen Listen anlegen. Titel auswählen, gewünschte Liste öffnen
  und „Zur Liste“ betätigen. Entfernen löscht nur Verknüpfungen, keine Audiodateien.

## Architektur und Grenzen

AudioHardwareService bleibt alleiniger BSP-/ES8311-Zugang. Der bereits gepinnte
esp-audio-player dekodiert auf einem gemeinsam nutzbaren, erst bei Wiedergabe
gestarteten Task. MediaSessionService vergibt den Audio-Fokus. Es gibt keinen
zusätzlichen Netzwerk- oder Scanner-Task. Ohne Wiedergabe keine Streamanfragen.
Der Decoder bekommt FILE-Callbacks; Lesen findet ausschließlich in den
vorhandenen NetworkWorker-/Transferqueues statt. Verspätete Antworten besitzen
ihren eigenen Puffer und können keine freigegebenen Decoderpuffer beschreiben.
Netzwerkstreams benutzen TLS-Zertifikatsprüfung, keine automatische Weiterleitung
und keine automatischen Wiedergabewiederholungen. Zugangsdaten in URLs werden
nicht unterstützt. Favoriten, Listen und Quellen verwenden ConfigService.

Begrenzt auf 256 Bibliothekstitel, 128 besuchte Verzeichnisse, acht Ebenen,
32 Favoriten/Sender und 32 Einträge je Wiedergabewarteschlange; Einstellungen
maximal 6000 JSON-Bytes. Ein großer Bibliotheksfilter wird in 32er-Warteschlangen
abgespielt. Bilder/Cover-Metadaten, Suchkatalog für Sender und eine Musik-Webseite
sind nicht Teil dieser Implementierung. NAS-Geschwindigkeit und Audioqualität
müssen mit echten Dateien am Zielgerät überprüft werden.

## CO₂-Waage

Das eigenständige lokale ESP32/HX711-Projekt steht in `devices/co2-scale`.
Die dortigen Kopien enthalten den vorgefundenen lokalen Waagenstand einschließlich
Weboberflächen, Konfiguration und Lizenzangaben. Es wird nicht in P4-Builds
einbezogen und niemals auf das Display geflasht.
