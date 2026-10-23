# Home Display für ESP32-P4

**Home Display 0.1.0** ist die erste öffentliche Version einer lokalen, tabletähnlichen Oberfläche für den Waveshare ESP32-P4 mit Touchdisplay. Sie bietet einen Desktop mit nativen Apps, Webverwaltung und Unterstützung für separat installierbare Lua-Apps.

> **Wichtig:** Die fertigen Downloads von Version 0.1.0 sind ausschließlich für bestätigte **ESP32-P4 pre-v3 / 360-MHz-Hardware** bestimmt. Bei unbekannter Hardware-Revision nicht flashen.

## Schnell starten

**Du hast bereits Home Display installiert?** Verwende ausschließlich die passende **OTA-Datei** und folge der [Anleitung zum Aktualisieren](docs/INSTALLATION.md#b--vorhandenes-home-display-aktualisieren).

**Du installierst Home Display auf einem neuen oder bewusst zurückzusetzenden Gerät?** Lies zuerst die [Anleitung zur Erstinstallation](docs/INSTALLATION.md#a--erstinstallation-auf-einem-neuen-gerät). Das **32-MiB-Factory-Image löscht interne Einstellungen und Daten**.

**Du möchtest den Quellcode selbst bauen?** Folge der [Entwickleranleitung](docs/PLATFORMIO_CODEX_SETUP.md).

**[Alle Downloads für Version 0.1.0](https://github.com/Michaelrossm/Home-Display-Releases/releases/tag/v0.1.0)** · **[Installation Schritt für Schritt](docs/INSTALLATION.md)** · **[Bekannte Einschränkungen](docs/RELEASE_0.1.0.md)**

## Unterstützte Hardware

- Waveshare ESP32-P4-WIFI6-POE-ETH-D mit 10,1-Zoll-DSI-Touchdisplay, 1280 × 800 Oberfläche.
- 32 MiB Flash und 32 MiB PSRAM entsprechend dem dokumentierten Hardwareprofil.
- Fertiges Release-Image: **pre-v3 / 360 MHz**. Das vorhandene v3.x-/400-MHz-Profil ist nur zum eigenen, passend geprüften Build vorgesehen.
- ESP32-C6 für WLAN ist ein **separater Chip mit separater Firmware**. Eine P4-OTA-Datei aktualisiert ihn nicht.

## Was ist enthalten?

Eine lokale Bedienoberfläche mit nativen Funktionen unter anderem für Dateien, Kalender, Telefonbuch, Rechner, Bilder und Einstellungen. Weitere Apps können separat als kompatible Lua-Pakete installiert werden. Nicht jede Integration ist vollständig oder auf jeder Hardwarekonfiguration getestet.

Die Sprechanlagenfunktion ist in diesem öffentlichen Release standardmäßig deaktiviert. Cloud-Konten benötigen die vom jeweiligen Anbieter verlangten Zugangsdaten und Freigaben; ein Cloud-Konto ist für die grundlegende lokale Verwendung nicht erforderlich.

## Download-Dateien richtig auswählen

| Datei | Zweck | Datenverlust |
|---|---|---|
| `*-ota.bin` | Bestehende P4-Firmware über den Web-Updater aktualisieren | Soll vorhandene Daten erhalten; vorher sichern |
| `*-factory.bin` | Vollständige Erstinstallation oder bewusstes Zurücksetzen bei bestätigter Hardware | **Ja: überschreibt interne Daten** |
| `*-signed.zip` | Komplettpaket mit Images, Prüfdateien und Lizenztexten | Inhalt je nach Anwendung |
| `SHA256SUMS` | Dateiprüfsummen vergleichen | Nein |

**Niemals das Factory-Image im normalen Web-OTA-Uploader auswählen.** Ein 360-MHz-Image niemals auf v3.x-/400-MHz-Hardware flashen.

## Fragen, Fehler oder Verbesserungen

Nutze die [GitHub-Issues](https://github.com/Michaelrossm/Home-Display-Releases/issues) und nenne Hardware-Revision, verwendete Firmwaredatei, Fehlermeldung und möglichst den relevanten Logauszug. Vor dem Hochladen von Logs IP-Adressen, Kennwörter, Zugangstokens und andere persönliche Daten entfernen.

## Entwickler und Quellcode

```powershell
python tools/dev.py setup
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py validate
python tools/dev.py build --env home_display_p4_legacy_dev
```

Weitere technische Informationen: [Dokumentation](docs/README.md), [Buildprofile](docs/PLATFORMIO_CODEX_SETUP.md), [Partitionen](docs/PARTITIONS.md), [C6-Anleitung](docs/C6_HOSTED_SLAVE_BUILD.md) und [Release-Prüfbericht](docs/RELEASE_0.1.0_VERIFICATION.md).

## Lizenz

Eigener Projektcode: [PolyForm Noncommercial 1.0.0](LICENSE.md); kommerzielle Nutzung ist nicht durch diese Lizenz erlaubt. Drittkomponenten behalten [ihre jeweiligen Lizenzen](THIRD_PARTY_NOTICES.md). Die Entwicklung wird unabhängig vom öffentlichen Release-Snapshot fortgeführt; die private Entwicklungs-Commit-Historie ist nicht Bestandteil dieses Repositorys.
