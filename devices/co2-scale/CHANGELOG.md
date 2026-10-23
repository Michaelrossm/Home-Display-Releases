# Changelog

## 1.0.13-recovery-2 – 20.10.2026

- RTC-Bootloop-Erkennung und eingeschränkter Software-Rückwechsel nach
  drei unbestätigten Starts nach OTA.
- OTA-Health-Check nach 120 Sekunden mit erreichbarem WLAN oder Hotspot.
- Optionaler IDF-Bootloader-Rollback-Bestätigungspfad, wenn der Bootloader
  dafür gebaut wurde (Standard-Arduino-Bootloader nicht automatisch umgestellt).
- OTA-Partitionsprüfung und dynamische Größenbegrenzung.
- Diagnose-API und Diagnosebereich in der Weboberfläche.
- JSON-Backup/-Import für Flaschenprofil, Kalibrierung, Zeitplan und NTP;
  ohne Zugangsdaten und ohne Änderung der statischen IP.
- Verbesserte OTA-Fortschrittsanzeige mit Neustart-/Health-Bestätigung.
- Bestehende Version 1.0.13, API-Kompatibilität, WLAN-Rettung und optionaler
  Passwortschutz bleiben erhalten.

- Weboberfläche: getrennte Übersicht und Einstellungen, kreisförmige Füllstandsanzeige, mobiloptimiertes Dashboard und Kalibrierung unter Einstellungen.
