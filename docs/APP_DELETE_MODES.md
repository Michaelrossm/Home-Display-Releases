# App-Löschmodi

Für installierte Lua-Apps gibt es zwei Löscharten.

## App löschen

Entfernt nur das installierte App-Paket.

Erhalten bleiben:

- normale App-KV-Einstellungen
- App-Secrets / Passwörter
- Dateien unter dem App-eigenen UserData-Pfad `apps/<app-id>`
- der gespeicherte Aktivierungszustand

Wird dieselbe App später erneut installiert und verwendet dieselben Schlüssel,
kann sie die bisherigen Einstellungen und Daten wiederverwenden.

## Komplett löschen

Entfernt:

- das App-Paket
- App-KV-Einstellungen
- App-Secrets
- Dateien unter `apps/<app-id>`
- den gespeicherten Aktivierungszustand

Gemeinsame Firmware-, Netzwerk-, Theme-, Kalender- oder Daten anderer Apps
werden nicht angefasst.

Der komplette Daten-Purge verwendet die gleichen per-App gehashten
KV-/Secret-Schlüssel und prüft die im Datensatz gespeicherte Owner/Key-Identität,
bevor ein Eintrag gelöscht wird.
