# Goliath-Sprechanlage

## Implementiert

- Die App `Goliath-Sprechanlage` ist im Launcher und unter **Einstellungen > Apps** registriert.
- Ihre Einstellungen liegen im bestehenden `ConfigService`; das Passwort liegt ausschließlich im vorhandenen Secret-Store. Es gibt keine Geräteadresse oder Zugangsdaten im Quellcode.
- Die HTTP-Verbindungsprüfung läuft über den vorhandenen `NetworkWorker`. Sie sendet nur einen lesenden `GET /`-Aufruf; sie konfiguriert die Außenstation nicht und sendet keine Tür-, Ruf- oder Gesprächsbefehle.
- `GoliathDiagnostics` ist ein flüchtiger, PSRAM-bevorzugter Ringpuffer: 256 Einträge / 44 KiB. Einträge tragen Sequenznummer und Uptime und werden nicht nach Flash, SD, LittleFS oder NVS geschrieben.

## Wiederverwendete Dienste

- `AppManager`, `UiTheme` und die vorhandene LVGL-Tastatur für Navigation und Formulare.
- `ConfigService` für Einstellungen und Secrets.
- `NetworkService` und `NetworkWorker` für Netzstatus und HTTP.
- Der bestehende `MediaSessionService` und `AudioHardwareService` bleiben der einzige Audio-Pfad; die Goliath-App erzeugt keinen zweiten Audiotreiber oder Task.

## Bewusst noch gesperrt

Livebild, Klingelerkennung, Gespräch, Mikrofon und Türöffner sind sichtbar als **nicht verfügbar**, aber technisch nicht implementiert. Im Projekt gibt es derzeit keinen RTSP-Client, H.264-Decoder oder Mikrofoneingang; zudem sind das Dahua-Klingel-/Gegensprechprotokoll und sichere Türöffnerbefehle für diese Station noch nicht bestätigt. Es werden keine unbekannten Befehle an die Station gesendet.

## RAM-Diagnose

Die App-Einstellung **RAM-Diagnose aktiv** einschalten und die Appseite **Diagnose** öffnen. Der vorhandene Datei-Webserver auf Port 8088 besitzt keine Authentifizierung; deshalb wurde dort absichtlich kein Log-Endpunkt angelegt. Ein Download/Browser-Endpunkt darf erst zusammen mit vorhandener Authentifizierung ergänzt werden.

Die Ringlog-Nachrichten enthalten keine Passwörter, Tokens oder vollständigen URLs.

## Nächste Hardware-Schritte

1. Reine Leseanalyse des tatsächlich ausgehandelten RTSP-Codecs/Profils mit dem Extra-Stream.
2. Eine kompatible ESP-IDF-Komponente für RTSP/H.264 prüfen und Messung von PSRAM, UI-Latenz und CPU auf dem P4 durchführen.
3. Dahua-Ereignis- und Gesprächsprotokoll mit einer nicht steuernden Diagnose erfassen.
4. Erst nach bestätigtem Protokoll: zentralen Anruf-Overlay-/Audiofokuspfad und ausdrücklich bestätigte Türöffneraktion ergänzen.

## Teststatus

`home_display_p4_r3_dev` baut erfolgreich: RAM 14.808 / 327.680 Bytes (4,5 %), Flash 2.381.010 / 11.534.336 Bytes (20,6 %). `python tools/dev.py test` und `python tools/dev.py validate` sind erfolgreich.

Hardware-/Protokollfunktionen gelten erst nach einem Test am ESP32 und ohne Eingriffe in bestehende Innenstationen als bestätigt.
