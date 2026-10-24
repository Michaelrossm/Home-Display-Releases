# Home-Display 0.1.0

Erster öffentlicher Quellcode-Snapshot mit neuer Historie. Weiterentwicklung bleibt
im privaten Entwicklungsrepository; öffentliche Snapshots enthalten keine private
Entwicklungshistorie und keine installierten Benutzer-/App-Daten.
Private gerätespezifische Sprechanlagen-Feldberichte werden nicht veröffentlicht;
der zugehörige Firmware-Quellcode bleibt vollständig enthalten.

## Hardware und Build

- Standard: P4 pre-v3 / v1.3, 360 MHz, `home_display_p4_legacy_dev` oder `_prod`.
- P4 v3.x / 400 MHz bleibt über `home_display_p4_r3_dev` oder `_prod` auswählbar.
- Binärdateien sind nicht austauschbar. Revision vor dem Flashen prüfen;
  bei unbekannter Hardware nicht flashen.
- Waveshare ESP32-P4-WIFI6-POE-ETH-D, 10,1-Zoll-DSI-Touch, UI 1280x800,
  32 MiB Flash und 32 MiB PSRAM.

```sh
python tools/dev.py setup
python tools/dev.py doctor
python tools/dev.py build --env home_display_p4_legacy_prod
python tools/dev.py test
python tools/dev.py validate --production
# Nur für bestätigte v3.x-Hardware:
python tools/dev.py build --env home_display_p4_r3_prod
```

Die festgelegten Frameworks und Werkzeuge liegen in `.pio/core`, getrennt von
anderen ESP32-Projekten. Es ist kein globaler Framework-Austausch nötig.
Die Projekt-CLI unter `.pio/cli` verwendet PlatformIO Core 6.1.19 passend zur
SCons-Version der Plattform. Nicht durch einen ungeprüften Core-Upgrade ersetzen.
In VS Code die mitgelieferte Aufgabe „Home-Display: Build pre-v3 DEV“ verwenden;
sie ist die Standard-Buildaufgabe und benutzt dieselbe geprüfte CLI.
Globale `PLATFORMIO_CORE_DIR`-/`PLATFORMIO_PACKAGES_DIR`-Overrides für einen normalen
frischen Checkout nicht setzen. Ein Build ist keine Hardwarevalidierung.

## Installation und Updates

Downloads für bestätigte pre-v3-Hardware (360 MHz):

- `home-display-0.1.0-p4-legacy-360mhz-ota.bin`: normales P4-OTA-Update.
- `home-display-0.1.0-p4-legacy-360mhz-factory.bin`: vollständiges 32-MiB-Image,
  ausschließlich für Erstinstallation oder absichtlichen Werksreset, Flash-Adresse
  `0x0`. Überschreibt auch NVS, Apps und interne Benutzerdaten; vorher sichern.
- `home-display-0.1.0-p4-legacy-360mhz-signed.zip`: obige Images, leere initiale
  Dateisysteme, signiertes Manifest, öffentlicher Prüfschlüssel und Lizenztexte.
- `SHA256SUMS`: Prüfsummen der veröffentlichten Downloads.

Die Factory-Datei niemals im normalen Web-OTA-Uploader auswählen. Die einzelnen
`appfs`-/`systemdata`-Images im Paket sind Initialisierungsdateien und kein
Benutzerdaten-Backup. Für 400 MHz werden mit diesem ersten Release keine
ungeprüften Binärdateien angeboten; das entsprechende Buildprofil bleibt erhalten.

- OTA: ausschließlich das zur Hardware passende P4-App-Image im vorhandenen
  Web-Firmware-Uploader auswählen, niemals ein Factory- oder C6-Image.
- Factory-Pakete ausschließlich zur Erstinstallation oder zum bewusst vollständigen
  Zurücksetzen verwenden. Partitionen und Dateisystem-Images überschreiben Daten.
- Die neue öffentliche Nummerierung `0.1.0` liegt numerisch unter den früheren
  Entwicklungsständen `0.3.x`. Der Uploader kann eine ausdrückliche Downgrade-
  Bestätigung verlangen; diese Prüfung bleibt erhalten.
- Konfigurationsschema bleibt 2. Benutzer, Kontakte, Termine, Konten und Passwörter
  sollen beim normalen OTA-Update erhalten bleiben.
- C6-Firmware und deren Whitelist bleiben unabhängig vom P4-Release.
- Kein automatisches Flashen durch Build-/Release-Werkzeuge.

## Funktionen und Grenzen

Sprechanlagen-App, Dienststart und Web-Routen sind standardmäßig deaktiviert.
Quellcode und bestehende Konfigurationen bleiben erhalten. Entwickler können die
Funktion bewusst über `custom_intercom_enabled = 1` unter `[env]` wieder aktivieren.

Native Apps sind enthalten; optionale Lua-Apps werden separat installiert.
Spotify-Weiter am externen Radio ist nicht bestätigt behoben. Native Musikwiedergabe
benötigt weitere praktische Format-/Audiotests. Ein 72-Stunden-Dauertest dieses
Releases wird nicht behauptet.

## Lizenzen und Signaturen

Der ursprünglich veröffentlichte Stand v0.1.0 wurde mit PolyForm Noncommercial License 1.0.0 ausgeliefert. Die spätere Umstellung des Repository-Quellcodes auf GPL-3.0-only ändert **nicht rückwirkend** die Lizenztexte in den bereits veröffentlichten v0.1.0-Binärdateien und Downloadpaketen. Für neu erstellte Builds gilt der aktuelle Quellcode-Lizenzstand aus `LICENSE.md`. Fremdsoftware behält ihre eigenen
Lizenzen; Originaltexte liegen unter `licenses/third-party` und bei den Originalquellen.
Die Übersicht ersetzt keine verbindlichen Fremdlizenzen oder rechtliche Prüfung.

Lizenzanzeige am Display: Einstellungen → System. Im Browser:
Einstellungen → System → Über Home-Display · Lizenzen.

Release-Manifeste können mit ECDSA P-256 signiert werden. Nur der öffentliche
Prüfschlüssel ist enthalten. Private Schlüssel niemals veröffentlichen.
Dies ist keine Aktivierung von Secure Boot oder eFuses. Der manuelle Web-Uploader
bleibt admin-/CSRF-geschützt und prüft Image und Hash. Kein automatischer signierter
Online-Updatefeed. `SHA256SUMS` prüft Integrität, nicht allein die Herkunft.

## Snapshot-Verfahren

Privat entwickeln und prüfen, dann aktuelle versionierte Quellen mit
`tools/create_release_snapshot.py --out LEERER_ORDNER` exportieren. Der Export
enthält keine `.git`-Historie, Build-Caches oder privaten Schlüssel. Der erste
öffentliche Snapshot bekommt einen eigenen Root-Commit; spätere Releases bauen
normal darauf auf. Niemals die private Git-Historie in dieses Repository pushen.
