# Web-Härtung 1–6

Umgesetzt:

1. Schreibende Web-Aktionen verwenden POST + CSRF.
   - Port 80 lädt einen per Boot zufälligen CSRF-Token über `/api/csrf`.
   - Port 8088 bettet denselben Token in seine Service-Seiten ein.
   - App-Toggle, Löschen, WLAN-Änderungen, Theme/Helligkeit, App-Uploads,
     Firmware-Upload/Reboot und Dateilöschen prüfen `X-HP-CSRF`.
   - Historische schreibende GET-Routen für Apps wurden auf POST umgestellt.
   - OAuth behält seinen bereits vorhandenen eigenen CSRF-Mechanismus.

2. Lua-App-Löschen ist zentralisiert.
   - `LuaPackageService::remove(id, purgeData)` ist die zentrale Paket-Löschlogik.
   - Port 80 und Port 8088 verwenden denselben Service.
   - `purgeData=false`: Paket löschen, Einstellungen/Daten behalten.
   - `purgeData=true`: zusätzlich App-KV, Secrets, UserData und Aktivierungsstatus löschen.

3. `web_settings.json` wird zentral und strenger validiert.
   - maximal 12 Tabs
   - maximal 48 Hauptfelder
   - maximal 96 Felder inklusive Repeater-Unterfeldern
   - doppelte Tab-IDs verboten
   - doppelte Haupt-Keys verboten
   - doppelte Repeater-Unterkeys verboten
   - Repeater-Key darf nicht mit Unterkey kollidieren
   - maximal 16 Repeater-Unterfelder / 16 Einträge
   - maximal 24 Select-Optionen
   - doppelte Select-Werte verboten
   - Passwortfelder innerhalb eines Repeaters verboten
   - Größen-/Text-/Key-Limits werden geprüft

4. ZIP-Vorprüfung im Browser.
   - `web_settings.json` wird bereits vor Upload auf Größe, JSON, Schema und
     dieselben wesentlichen Strukturregeln geprüft.
   - Gilt für Port 80 und die Service-Seite.

5. Installer-Fehler sind im Web verständlich.
   - technische Codes wie `web_settings_schema` werden in deutsche Meldungen übersetzt.
   - Lua-Syntax-, Staging-, Schema- und Installerfehler werden verständlicher ausgegeben.

6. Websettings werden nach Reinstall/Update neu geladen.
   - Ist die Web-Einstellungsseite der aktualisierten App geöffnet, wird sie nach
     erfolgreichem Commit automatisch aus der neuen `web_settings.json` neu aufgebaut.

Hinweis:
`/firmware/inspect` bleibt POST ohne CSRF, weil es nur die hochgeladenen
Firmware-Header prüft und keinen persistenten Zustand verändert.
