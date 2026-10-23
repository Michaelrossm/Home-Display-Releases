# Release 0.1.0: Prüfstand vom 23.10.2026

- `python tools/dev.py doctor`: erfolgreich, projektlokale CLI und Paketablage.
- Produktionsbuild `home_display_p4_legacy_prod`: erfolgreich im Entwicklungsbaum
  und in einem separaten Checkout des öffentlichen Quellcode-Snapshots.
- Der separate Checkout hatte zu Beginn keine Buildobjekte. Der bereits separat
  heruntergeladene, festgelegte Toolchain-Cache wurde wiederverwendet; dies ist
  kein Nachweis bitidentischer Builds oder eines vollständig leeren Paket-Caches.
- Tatsächlich erzeugte Konfiguration: pre-v3 (`REV_MIN_100`), 360 MHz,
  FreeRTOS 1.000 Hz, ESP-IDF 5.5.5; App-Image meldet Version 0.1.0 und Chip ESP32-P4.
- Veröffentlichtes OTA-Image aus dem separaten Checkout: 6.417.280 Byte;
  Reserve je 9-MiB-OTA-Slot: 3.019.904 Byte.
- 486 Gesamttests: erfolgreich, zwei Decoder-Tests zunächst wegen fehlendem
  Pillow übersprungen. Pillow ist jetzt festgelegt; beide betroffenen Tests wurden
  anschließend in vier gezielten Tests ohne Skip erfolgreich nachgeprüft.
- Vier zusätzliche Architekturprüfungen: erfolgreich.
- Produktionsvalidator mit finalem OTA-, SystemData- und AppFS-Image:
  `VALID Home-Display 0.1.0; config schema 2; production=True`.
- ECDSA-Manifestsignatur, Payload-Hashes, ZIP-CRC und alle Download-Prüfsummen:
  erfolgreich geprüft. Nur der öffentliche Schlüssel wird veröffentlicht.
- Vollständiges Factory-Image: 32 MiB. Bootloader-/Partitionstabellen-Präfix
  entspricht dem generierten Factory-Image; OTA-, AppFS- und SystemData-Inhalte
  stimmen an den geprüften Partitionsadressen bytegenau mit den Einzelimages überein.
- IR-Tracker-Projektlizenz unverändert übernommen; Originaltexte der Drittsoftware
  separat mitgeliefert. Lizenzanzeige in Display-Systemeinstellungen und Browser.

Nicht nachgewiesen: Hardware-Dauertest, vollständige Audiowiedergabe aller Formate,
400-MHz-Build auf diesem Rechner oder Betrieb auf v3.x-Hardware. Die entsprechenden
Buildprofile bleiben vorhanden. Sprechanlage ist standardmäßig deaktiviert.
Es wurde für diesen Auftrag kein Gerät geflasht und kein Benutzerspeicher gelöscht.
