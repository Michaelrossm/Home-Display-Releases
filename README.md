# Home-Display ESP32-P4 — VNext

Lokale Firmware für Waveshare ESP32-P4-WIFI6-POE-ETH-D mit 10,1-Zoll-DSI-Touchdisplay.
Firmwareversion: 0.1.0, Konfigurationsschema 2. Release-Vorbereitung; ein erfolgreicher
Build ersetzt keine Prüfung am echten Gerät.

Release-Hinweise und Hardwareprofile: [Version 0.1.0](docs/RELEASE_0.1.0.md).
Projektlizenz: [PolyForm Noncommercial 1.0.0](LICENSE.md).
[Drittsoftware](THIRD_PARTY_NOTICES.md) behält ihre jeweiligen eigenen Lizenzen.

## Bauen und prüfen

```powershell
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py validate
python tools/dev.py build --env home_display_p4_legacy_dev
```

Der aktuelle Standard ist **Legacy / 360 MHz** (P4 vor v3, einschließlich v1.3).
P4-v3.x benötigt ausdrücklich `--env home_display_p4_r3_dev` (400 MHz).
Die Binärdateien sind nicht austauschbar. Bei unbekannter Revision nicht flashen.
Details: [Build-Setup](docs/PLATFORMIO_CODEX_SETUP.md), [Agentenregeln](AGENTS.md).

## Architektur

- Ein Waveshare-BSP/LVGL-Task; keine zweite Display- oder Touchverwaltung.
- Gemeinsame Dienste für Storage, Konfiguration/Secrets, Netzwerk, Bilder und Audio.
- Native System-Apps und gemeinsame Lua-/App-Infrastruktur in `src/`.
- Installierbare Lua-Apps sind externe Pakete und werden hier nicht veröffentlicht.
- C6: separat gepflegte ESP-Hosted-Firmware; siehe [C6-Anleitung](docs/C6_HOSTED_SLAVE_BUILD.md).
- OTA bleibt A/B- und BootHealth-gesichert; beschädigte Datenträger werden nicht automatisch formatiert.

## Speicher

32 MiB Flash: je **9 MiB OTA A/B**, **9 MiB AppFS**, **4,8125 MiB SystemData**,
zuzüglich NVS, OTA-Metadaten, PHY und Coredump.
Verbindlich ist [partitions_32mb.csv](partitions_32mb.csv).
Eine Änderung dieses Layouts ist keine normale OTA-Aktualisierung:
[Migration und Warnungen](docs/PARTITIONS.md).

SD/USB/NAS verwenden die zentralen Dateidienste. Persönliche Daten, Zugangsdaten,
lokale App-Pakete und Build-Caches gehören nicht ins Repository.

## Dokumentation

Der [Dokumentationsindex](docs/README.md) führt zu aktuellen APIs, Einrichtung und
Diagnose. [Prüfstand](docs/VALIDATION.md) und [Änderungshistorie](CHANGELOG.md)
sind von Bedienungs- und API-Anleitungen getrennt.

Aktuelle Ergänzungen:
- [Gemeinsame Bildpipeline und Mehrfach-Uploads](docs/photos-performance.md)
- [Adaptive Widget-Aktualisierung und Vorschaugrößen](docs/adaptive-widget-rendering.md)
- [Sprachpakete](docs/language-packs.md)
- [Lua-App-Pakete / Schema 3](docs/LUA_APP_PACKAGES.md)
- [HTTPS und Web-Anmeldung](docs/web-https.md)

## Releases

`python tools/dev.py validate` prüft den Entwicklungsstand.
Produktionsvalidierung benötigt zusätzlich einen echten Signaturschlüssel und
Hardwareabnahme. Entwicklungs-Binärdateien nicht als produktionsvalidiert behandeln.
