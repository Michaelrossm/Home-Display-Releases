# Dokumentation

Dieser Index verweist auf die gepflegten Einstiegs- und API-Dokumente.
Versionierte technische Notizen beschreiben ihren damaligen Entwicklungsstand;
für Build, Partitionen und aktuellen Prüfstand gelten die folgenden Quellen.

## Installation für Benutzer

- [Home Display installieren oder aktualisieren – Schritt für Schritt](INSTALLATION.md)
- [Erster öffentlicher Download: Version 0.1.0](RELEASE_0.1.0.md)

## Einrichtung und Architektur

- [Projektübersicht](../README.md), [Entwicklungsregeln](../AGENTS.md)
- [Build und Hardwareprofile](PLATFORMIO_CODEX_SETUP.md)
- [Architektur](architecture.md), [Waveshare-BSP](WAVESHARE_BSP_INTEGRATION_0.3.3.md)
- [C6-Firmware](C6_HOSTED_SLAVE_BUILD.md)
- [Partitionen und Migration](PARTITIONS.md), [Storage](storage.md)
- [Sicherheit](SECURITY.md), [Update und Recovery](update-recovery.md)
- [Aktueller Prüfstand](VALIDATION.md)

## Gemeinsame APIs und Web

- [Lua-Paketstandard](LUA_APP_PACKAGES.md), [Lua-API](LUA_APP_API.md)
- [VNext-App-Engine](VNEXT_ENGINE_V3.md), [Lifecycle](APP_LIFECYCLE.md)
- [Medien-/Storage-/Bild-API](MEDIA_STORAGE_API.md)
- [App-Weboberflächen](NATIVE_APP_WEB.md), [Web-Einstellungen](WEB_APP_SETTINGS.md)
- [HTTPS und Anmeldung](web-https.md), [Web-Standby](web-multi-client-standby.md)
- [Sprachpakete](language-packs.md)

## Funktionen

- [Bilder](native-photos.md), [Bildpipeline und Uploads](photos-performance.md)
- [Dateien](file-explorer.md), [SD-/USB-Ordnerstruktur](external-storage-layout.md)
- [Drucker-Service](printer_service.md), [App-Anbindung](printer-panel.md)
- [Druckeraktionen](printer-typed-controls.md), [Druckfarm](PRINTER_FARM_QUEUE_SAFETY.md)
- [Kalender](NATIVE_CALENDAR.md), [Termindetails](appointment-details.md)
- [Telefonbuch](NATIVE_PHONEBOOK.md), [Benutzerverwaltung](shared-user-management.md)
- [Konten](cloud-accounts.md), [Synchronisation](oauth-calendar.md)
- [Sprechanlagen-Architektur (derzeit deaktiviert)](INTERCOM_ARCHITECTURE.md), [Taschenrechner](native-calculator.md)

## Performance und Diagnose

- [Adaptive Widgets](adaptive-widget-rendering.md)
- [Lastverteilung P4/C6](performance-p4-c6.md)
- [Energiesparpolitik](POWER_POLICY.md), [Adaptive Eco](ADAPTIVE_ECO.md)
- [Test-Szenarien](scenario-matrix.md)

## Bereinigung

Alte Überschreib-Patches/Einmal-Installer, überholte Validierungsberichte und
unbenutzte native Strom-/Diagnose-Oberflächen wurden entfernt. Die bisherigen
Versionen sind in Git wiederherstellbar. Aktive Backends, Hardwarehilfen,
Recovery, Sicherheitsprüfungen und Regressionstests bleiben erhalten.

Lokale Apps, ZIPs und Build-Caches in ignorierten Verzeichnissen werden nicht
hochgeladen. Sie wurden nicht pauschal gelöscht, weil sie eigene Arbeiten enthalten
können. Installierbare Apps gehören nicht in dieses Firmware-Repository.

Die Bereinigung vom 19.10.2026 entfernt zusätzlich den ungenutzten nativen
Home-Platzhalter, die unbenutzte E-Mail-Hash-ID-Bildung für Benutzer und die
Paketierer der abgelösten Lua-Kalender-/Radio-Versionen. Die zentralen stabilen
Benutzer-IDs, Migration vorhandener Datensätze und öffentlichen Lua-APIs bleiben
erhalten. Der optionale Drucker-Paketierer verwendet die aktuelle professionelle
App; ältere lokale Rechner-, Kalender-, Radio- und Druckerpakete wurden außerhalb
des Repositories gesichert. Aktive Backends, native Apps und aktuelle externe
App-Quellen bleiben erhalten.

Tests ausschließlich für diese ausgemusterten Lua-Oberflächen und deren alte
UI-Doubles wurden entfernt. Die nativen Kalender-/Rechnerprüfungen, gemeinsame
Lua-Runtime-, Sicherheits-/Rollbacktests und die Laufzeittests der aktuellen
professionellen Drucker-App bleiben aktiv; Paketprüfungen verwenden die aktuelle
Druckerquelle statt des alten Pakets.
