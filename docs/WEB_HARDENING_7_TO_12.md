# Web-/App-Härtung 7 bis 12

## 7. Komplett-Löschen sicherer und zweiphasig

Beim vollständigen Löschen einer Lua-App wird zuerst der komplette App-eigene
Datenbereich geprüft. Erst wenn KV-Index, Secret-Index, Owner/Key-Zuordnung und
UserData-Struktur plausibel sind, startet die Löschung.

Ablauf:
1. Preflight der App-Daten.
2. KV/Secrets/UserData löschen.
3. App-Paket löschen.
4. Erst danach Registry/UI aktualisieren.

Schlägt der NVS-/Datei-Purge fehl, bleibt das App-Paket installiert. Die
Fehlermeldung nennt zusätzlich, wie viele KV-/Secret-Einträge bereits entfernt
wurden. Damit gibt es nicht mehr den alten Zustand „Programm weg, Daten-Purge
danach fehlgeschlagen“.

## 8. App-Daten-Export

Port 80 und Port 8088 bieten „Daten sichern“.

Exportiert werden:
- normale KV-Einstellungen inklusive Typ und Wert
- App-Dateien unter `apps/<app-id>` inklusive Base64-Inhalt
- Namen vorhandener Secrets und deren Status

Secret-Werte selbst werden aus Sicherheitsgründen niemals exportiert.

Vor „Komplett löschen“ versucht die Port-80-Oberfläche automatisch zuerst ein
Backup herunterzuladen. Schlägt das Backup fehl, startet das Komplett-Löschen
nicht.

## 9. Web-Diagnose

Der Supportbericht enthält jetzt zusätzlich:
- Port 80 aktiv/inaktiv
- Port 8088 aktiv/inaktiv
- HTTP-Control-Ports
- aktive Clients
- freie Socket-Plätze
- konfigurierte maximale Socket-Anzahl
- CSRF-Fehler

Die Diagnose-Seite zeigt die wichtigsten Werte zusätzlich als Kennzahlen.

## 10. Theme endgültig entkoppelt

`UiTheme` enthält keine UI-Erkennung und erzeugt keine Theme-Schalter mehr.

Aufteilung:
- `UiTheme`: Palette, Farben, Styles, Recoloring
- `ThemeController`: Speichern + Anwenden + UI-Refresh
- `ThemeControls`: explizite Kontrollzentrum-Bedienung
- `SettingsWidgets`: Einstellungsseite

Das Kontrollzentrum bekommt die Theme-Controls explizit von `UiShell`; es gibt
keine „zweiter Screen“-/Größen-/Parent-Heuristik mehr.

## 11. Touch-Start / Idle

Der aktuelle `UiShell` registriert bereits für alle Pointer-Input-Devices
`LV_EVENT_PRESSED -> touchActivity` und der Saver selbst reagiert ebenfalls auf
`LV_EVENT_PRESSED`.

Deshalb wurde bewusst **kein zweiter Touch-Callback** hinzugefügt. Der bereits
vorhandene globale Touch-Start erfüllt diesen Punkt ohne Doppelimplementierung.
Der zuvor ergänzte atomare Idle-Race-Schutz bleibt bestehen.

## 12. AppRegistry im Webserver nur noch als Snapshot

HTTP-Handler führen kein `AppRegistry::rebuild()` mehr aus.

Neu:
- `AppRegistry::snapshot()`
- `AppRegistry::lookup()`
- Snapshot-Zugriffe sind über ein Mutex geschützt
- Web-Toggle/Web-App-Löschen aktualisieren den vorhandenen Snapshot direkt
- vollständiges `rebuild()` bleibt UI-/Installer-Pfaden vorbehalten

Der Lua-Installer darf nach erfolgreichem Commit weiterhin im UI-Pfad
`rebuild()` ausführen.
