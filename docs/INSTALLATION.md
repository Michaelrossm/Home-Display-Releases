# Home Display 0.1.0 installieren – Schritt für Schritt

Diese Anleitung ist für alle gedacht, die **Home Display auf einem passenden Waveshare ESP32-P4 verwenden** möchten. Du musst nicht programmieren können, um eine bereits installierte Firmware zu aktualisieren. Eine vollständige Erstinstallation benötigt allerdings einen PC mit USB-Verbindung und einem Flash-Werkzeug.

## 1. Vorher unbedingt prüfen

**Prüfe die Hardware-Revision**, bevor du eine Firmware auswählst. Die fertigen Dateien dieses Releases passen ausschließlich zur bestätigten **pre-v3-Variante mit 360 MHz**. Für **ESP32-P4 v3.x / 400 MHz** gibt es in Release 0.1.0 keine fertig geprüften Downloads. **Ist die Revision nicht sicher bekannt: nicht flashen.**

Die dokumentierte Zielhardware ist Waveshare ESP32-P4-WIFI6-POE-ETH-D mit 10,1-Zoll-DSI-Touchdisplay, 32 MiB Flash und 32 MiB PSRAM. Andere Displays, Boardvarianten oder Flashgrößen sind nicht automatisch kompatibel.

**Vor jedem Update:** Eigene Apps, Einstellungen, Kontakte, Kalender und Zugangsdaten möglichst sichern. Eine nicht erfolgreiche Installation oder ein Zurücksetzen kann Daten kosten. Gerät während des Schreibens zuverlässig mit Strom versorgen.

[Hier Release 0.1.0 öffnen und Dateien herunterladen](https://github.com/Michaelrossm/Home-Display-Releases/releases/tag/v0.1.0).

## 2. Welche Datei brauche ich?

| Download | Verwendung |
|---|---|
| `home-display-0.1.0-p4-legacy-360mhz-ota.bin` | Update eines Geräts mit funktionierendem Home-Display-Webserver |
| `home-display-0.1.0-p4-legacy-360mhz-factory.bin` | Neues Gerät initial flashen oder bewusst alle internen Daten löschen |
| `home-display-0.1.0-p4-legacy-360mhz-signed.zip` | Komplettpaket mit Zusatzimages, signiertem Manifest, Prüfschlüssel und Lizenzen |
| `SHA256SUMS` | Optional: Prüfsumme des Downloads kontrollieren |

**Verwechslungsgefahr:** Die Factory-Datei ist ein komplettes **32-MiB-Flash-Abbild**. Sie darf **nicht** über den normalen OTA-Uploader installiert werden.

## A – Erstinstallation auf einem neuen Gerät

**Achtung: Hierbei werden sämtliche Inhalte des internen 32-MiB-Flashs überschrieben, einschließlich gespeicherter Apps, Benutzerdaten und Zugangsdaten.** Diesen Weg nicht für ein normales Update verwenden.

1. Überprüfe, dass das Board tatsächlich **pre-v3 / 360 MHz** und mit dem genannten Hardwareprofil kompatibel ist.
2. Lade die Datei `home-display-0.1.0-p4-legacy-360mhz-factory.bin` herunter.
3. Verbinde den **Programmier-/USB-Anschluss des ESP32-P4** entsprechend der Waveshare-Boardanleitung mit deinem Windows-, Linux- oder macOS-PC. Falls nötig, versetze das Board gemäß Herstelleranleitung in den Downloadmodus.
4. Installiere auf dem PC Python und das Flash-Werkzeug **esptool**: `python -m pip install esptool`.
5. Öffne ein Terminal im Ordner der heruntergeladenen Datei und ermittle den richtigen seriellen Port: unter Windows beispielsweise `COM5`, unter Linux beispielsweise `/dev/ttyACM0`. **Nur den P4-Port verwenden – nicht den separaten C6-Programmieranschluss.**
6. Führe für einen bestätigten P4-Anschluss den folgenden Befehl aus und ersetze `COM5` durch deinen Port:

   ```powershell
   python -m esptool --chip esp32p4 --port COM5 --baud 460800 write_flash 0x0 home-display-0.1.0-p4-legacy-360mhz-factory.bin
   ```

7. Warte, bis das Flash-Werkzeug den Vorgang erfolgreich bestätigt. Erst danach die Verbindung lösen beziehungsweise das Board gemäß Herstelleranleitung neu starten.
8. Beim ersten Start kann die Einrichtung etwas Zeit benötigen. Folge den Anzeigen auf dem Display.

**Wenn der P4 nicht erkannt wird:** Port, USB-Datenkabel, Board-Stromversorgung und den herstellerspezifischen Downloadmodus prüfen. Nicht auf Verdacht eine andere Hardware-Firmware verwenden.

**WLAN/ESP32-C6:** Der ESP32-C6 besitzt ein eigenes Flash. Das Factory-Image oben programmiert **nur den P4**. Wenn WLAN nicht funktioniert oder der C6 die falsche Firmware besitzt, gilt die separate [C6-Installationsanleitung](C6_HOSTED_SLAVE_BUILD.md) mit anderen Anschlüssen und Dateien. Die C6-Anleitung ist technischer und sollte sorgfältig gelesen werden.

## B – Vorhandenes Home Display aktualisieren

Dies ist der **empfohlene Weg, wenn Home Display auf deinem Gerät bereits läuft**.

1. Prüfe erneut das Hardwareprofil: **pre-v3 / 360 MHz**.
2. Sichere wichtige Einstellungen, Apps und persönliche Daten nach Möglichkeit.
3. Lade **nur** `home-display-0.1.0-p4-legacy-360mhz-ota.bin` herunter.
4. Öffne den vorhandenen Home-Display-Webserver im Browser. Verwende die **HTTPS-Adresse beziehungsweise den Hostnamen, den dein Gerät anzeigt**. Der normale HTTPS-Verwaltungszugang ist in [HTTPS und Web-Anmeldung](web-https.md) beschrieben.
5. Melde dich mit den **bereits für dein Gerät eingerichteten** Verwaltungszugangsdaten an. Falls ein voreingestelltes Passwort noch aktiv ist, ändere es unbedingt vor der weiteren Netzwerknutzung.
6. Öffne den Firmware-/Updatebereich der vorhandenen Weboberfläche, wähle die `*-ota.bin`-Datei und beachte alle Prüfungen und Warnungen.
7. Warte, bis Schreiben und Verifizieren ausdrücklich abgeschlossen sind. Gerät nicht ausschalten, Browser-Upload nicht unnötig wiederholen, keinen Factory-Reset auslösen.
8. Bestätige den Neustart, falls er angeboten wird. Prüfe anschließend die angezeigte Firmwareversion und wichtige Daten/Apps.

**Hinweis für bisherige private Entwicklungsstände:** Version `0.1.0` ist die **erste öffentliche Veröffentlichung**. Private Entwicklungs-Builds können höhere Nummern wie `0.3.x` gehabt haben. Ein entsprechender Downgrade-Hinweis ist möglich und muss bewusst geprüft werden. Die öffentliche Versionierung beginnt trotzdem bei `0.1.0`.

Das OTA-Image ersetzt die P4-App-Firmware. Es ist **kein** Abbild deiner Einstellungen und aktualisiert **nicht automatisch den C6**.

## C – Wenn etwas nicht funktioniert

- **Gerät startet nicht:** Stromversorgung prüfen; serielles P4-Protokoll sichern. Hinweise zu [Update und Recovery](update-recovery.md).
- **OTA wird abgelehnt:** Hardwareprofil, Dateiendung und Imagegröße prüfen; keinesfalls das Factory-Image als Ausweichlösung in den OTA-Uploader laden.
- **Display bleibt weiß / startet wiederholt:** Nicht ungeprüft weitere Images flashen; Resetursache und serielles Log auswerten.
- **WLAN/C6 fehlt:** [Separate C6-Firmware-Anleitung](C6_HOSTED_SLAVE_BUILD.md) beachten.
- **Browser meldet Zertifikatswarnung:** Das Gerät nutzt ein eigenes HTTPS-Zertifikat. Den Fingerabdruck vertrauenswürdig überprüfen, nicht einfach Warnungen ignorieren. Siehe [HTTPS-Dokumentation](web-https.md).
- **Unklar, ob dein Board kompatibel ist?** **Nicht flashen.** Öffne ein [GitHub-Issue](https://github.com/Michaelrossm/Home-Display-Releases/issues) mit Boardbezeichnung, Revision und Fotos der Beschriftung – ohne private Daten.

## D – Für Entwickler

Wenn du den Quellcode selbst bauen möchtest:

```powershell
python tools/dev.py setup
python tools/dev.py doctor
python tools/dev.py build --env home_display_p4_legacy_dev
```

Mehr dazu: [PlatformIO-Build](PLATFORMIO_CODEX_SETUP.md), [Release 0.1.0](RELEASE_0.1.0.md), [Partitionen](PARTITIONS.md).

**Stand:** Anleitung zum ersten öffentlichen Release 0.1.0; kein Anspruch auf erfolgreichen Flashvorgang ohne Hardwareprüfung. Firmware, Partitionierung und Update-Sicherheitsmechanismen werden durch diese Dokumentation nicht verändert.
