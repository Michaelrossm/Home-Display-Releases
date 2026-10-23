# Testplan für die erste Inbetriebnahme nach dem Reparatur-Flash

**Voraussetzung:** USB-Datenverbindung oder bereits erreichbares WLAN.
Bei derzeit nicht erreichbarer Waage kann das ZIP nicht selbst installiert werden.

## Ohne Eingriff in gespeicherte NVS-Daten

1. PlatformIO-Projekt öffnen, Abhängigkeiten installieren, Build ausführen.
2. `firmware.bin`-Größe mit OTA-Zielgröße in `/api/diagnostics` vergleichen.
3. Bei USB-Erstinstallation `Upload` ohne `Erase Flash` verwenden.
4. Nach dem Neustart mindestens 120 Sekunden warten.
5. `GET /api/v1/status` prüfen: `firmware_build=1.0.13-recovery-2`.
6. `GET /api/diagnostics` prüfen: `ota_slots_valid=true`, `boot_confirmed=true`.
7. Unter `Backup und Wiederherstellung` eine JSON-Datei herunterladen.
8. Webpasswort testweise aktivieren; Verwaltungsseiten geschützt, drei JSON-GETs frei.
9. Gewichte mit einem unabhängigen Referenzgewicht überprüfen.
10. WLAN testweise nicht erreichbar machen und Rettungs-Hotspot kontrollieren.
11. Nachtabschaltung nach gültiger Uhrzeitsynchronisierung kontrollieren.
12. OTA nur mit einem nachweislich passenden Image durchführen und
    Stromversorgung bis nach Bootbestätigung nicht trennen.

## Negative Tests

- Nicht-JSON-Datei importieren: 400, Einstellungen bleiben erhalten.
- Ungültige Kalibrierpunkte importieren: 400, Einstellungen bleiben erhalten.
- Fremder Origin / fehlender CSRF bei POST: 403.
- Zu großes OTA-Image: Update wird abgebrochen, bisherige App bleibt aktiv.
- Keine zweite OTA-Partition: Update wird abgelehnt.
- Passwort eingerichtet: alle Verwaltungs-APIs verlangen Anmeldung.

## Nicht automatisch getestet

- Der echte ESP32-Bootloader, die physische Wägezelle, die Flash-Größe,
  das Rückschalten auf die vorherige Partition und die Stromversorgung.
- Absturz **vor** `setup()` (ohne eigens gebauten Rollback-Bootloader nicht
  automatisch reparierbar).
