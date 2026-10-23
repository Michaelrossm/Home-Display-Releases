# Wiederherstellung und OTA-Strategie (v1.0.13-recovery-2)

## Vor dem ersten Update

1. **Aktuell offline?** Ohne WLAN, USB-Datenverbindung oder anderen Flash-Zugang
   kann diese ZIP die bereits installierte Firmware nicht verändern. Waage bei
   Bedarf einmalig öffnen und USB-Datenkabel anschließen.
2. In PlatformIO **Upload** benutzen, **nicht Erase Flash**. Kalibrierung und
   Einstellungen liegen in NVS. Trotzdem nach Wiederherstellung JSON-Backup anlegen.
3. Das bestehende Partitionslayout **nicht blind ändern**. `esp32dev` benötigt
   für OTA zwei gültige OTA-App-Slots. Eine neue Partitionstabelle kann über
   OTA nicht zuverlässig migriert werden. Vorher die realen Partitionen prüfen.
4. Das neue Firmware-Build-Kennzeichen ist `1.0.13-recovery-2`.

## Was die Firmware automatisch macht

- Beim Start werden Resetgrund und RTC-Bootzähler erfasst.
- Nach OTA bleibt `otarescue` gesetzt, bis mindestens 120 Sekunden gelaufen
  sind und Heim-WLAN oder ein aktiver Rettungs-Hotspot verfügbar ist.
- Bei drei aufeinanderfolgenden unbestätigten Boots versucht die Anwendung,
  die vorherige gültige OTA-Partition wieder als Bootziel zu setzen.
- Falls das nicht klappt, wird der Rettungs-Hotspot ohne Nachtabschaltung
  gestartet. Das setzt voraus, dass `setup()` noch erreicht wird.
- Fehlgeschlagene AP-Starts werden alle 15 Sekunden wiederholt.
- Ein laufender OTA-Upload verhindert eine geplante WLAN-Abschaltung.
- OTA wird nur mit zwei App-Slots gestartet. Uploads größer als die
  Zielpartition werden abgebrochen. `Update.end(true)` prüft das Image.
- Diagnosen: `/api/diagnostics` (mit Passwort, falls konfiguriert).
- Backup: `GET/POST /api/backup` (geschützt bei gesetztem Passwort, POST mit CSRF).

## Wichtige Grenze: Kein vollständiges Bootloader-Rollback mit Standard-Arduino

Der in PlatformIO bei `framework = arduino` gelieferte **vorkompilierte**
Bootloader hat nicht zwangsläufig `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.
Ein `sdkconfig.defaults` im reinen Arduino-Projekt schaltet dies **nicht**
zuverlässig ein. Deshalb kann die Software nur zurückwechseln, wenn die
Firmware weit genug bootet, um die Bootloop-Erkennung in `setup()` auszuführen.
Ein sofortiger Crash im Bootloader oder vor `setup()` kann weiterhin eine
manuelle Wiederherstellung per USB erfordern.

Für echtes Bootloader-Rollback muss eine passend konfigurierte und getestete
ESP-IDF/Arduino-as-component-Toolchain samt Bootloader verwendet werden. Diese
ZIP ändert Bootloader oder Partitionstabelle absichtlich **nicht**, um
bestehende Installationen nicht zu gefährden. Bei aktiviertem IDF-Rollback
bestätigt der Code nach dem Health-Check die neue App mit
`esp_ota_mark_app_valid_cancel_rollback()`.

## Sicherheits- und Datenschutzdetails

- Bei eingerichtetem Passwort ist die gesamte Webverwaltung geschützt;
  JSON-Status, JSON-Live und JSON-Uhrzeit bleiben absichtlich öffentlich.
- Ohne Passwort ist auch OTA erlaubt (lokales vertrauenswürdiges Netz vorausgesetzt).
- Backup enthält **keine** WLAN-Passwörter, statische IP oder Admin-Passwörter.
- JSON-Import prüft Schema, Grenzwerte und Monotonie der Kalibrierpunkte,
  bevor bestehende Werte überschrieben werden.
- Kein offener Router-Port. HTTP Basic Auth ist ohne TLS nicht verschlüsselt.
- Bei ungültigen Importdaten bleibt die vorhandene Konfiguration unverändert.

## Manuelle Tests nach dem USB-Upload

1. WLAN einschalten und `/api/v1/status` abrufen.
2. Weboberfläche mit und ohne Passwort testen; JSON bleibt frei.
3. JSON-Backup erstellen und an einem sicheren Ort ablegen.
4. Nach Kalibrierung ein unabhängiges Gewicht prüfen.
5. OTA mit gültigem, passendem Image testen; Strom währenddessen nicht trennen.
6. Nach 120 Sekunden Diagnose prüfen: `boot_confirmed=true`.
7. WLAN testweise ausschalten: Rettungs-Hotspot muss erscheinen.
8. Nachtabschaltung mit einer gültigen Uhrzeit testen.

**Ungetestet:** Vollständiger PlatformIO-Build, Flash-Größe, tatsächliches
Rollback und Verhalten auf der realen ESP32-Hardware.
