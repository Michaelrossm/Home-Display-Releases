# Zweiter Firmware-Updateweg – SD / USB

## Dateien und Priorität

- SD-Karte bevorzugt; falls keine SD eingebunden ist, angeschlossenen USB-Stick verwenden.
- Verzeichnis **`/Firmware`** wird bei externer Speichervorbereitung angelegt.
- **`/Firmware/Firmware-P4.bin`** – reguläre **P4-OTA-App-BIN**, kein Factory-/Merged-Image.
- **`/Firmware/Firmware-C6.bin`** – ausschließlich das projektspezifische, freigegebene
  ESP-Hosted-C6-Image `esp32c6-v2.12.13.bin` (1.226.976 Bytes und fest vorgegebener SHA-256).
- Die Dateien werden direkt auf SD/USB kopiert. Dieser lokale Updateweg bleibt
  unabhängig vom direkten Web-Updater erhalten.

## Webserver

Der vorhandene P4-Uploader prüft zunächst Produkt/Chip/Revision, Größe und lokale
SHA-256. Er schreibt direkt in den inaktiven P4-A/B-OTA-Slot, prüft den Flash-Inhalt
erneut und aktiviert erst danach den Boot-Slot. Das funktioniert unabhängig von
SD/USB. Die bestehende Fortschrittsanzeige am Display bleibt bis zur bestätigten
Neustartaktion sichtbar.

Der aktuelle Web-Updater bietet nur P4-OTA an. C6-Dateien werden weiterhin über
den lokalen SD-/USB-Updateweg nach Prüfung von Größe und freigegebenem Hash
installiert. Während der C6-SDIO-Aktivierung kann die Netzwerkverbindung ausfallen.

## Am Display

**Einstellungen → Updates und Wiederherstellung** zeigt P4/C6-Dateien auf dem
bevorzugten Datenträger und den Installationsstatus. P4 lokal installieren schreibt
nur den zweiten OTA-Slot; der anschließende P4-Neustart muss separat bestätigt werden.
C6 lokal installieren startet den unveränderten ESP-Hosted-SDIO-Pfad und führt nach
Aktivierung einen Neustart aus. Die reguläre Online-C6-Aktualisierung bleibt bestehen.

## Sicherheit und bekannte Grenzen

- Nie das aktive OTA-App-Image überschreiben; P4 prüft Flash-SHA-256 vor dem Boot-Slot-Wechsel.
- C6-Image wird **vor** Beginn des SDIO-Schreibens vollständig anhand SHA-256 geprüft.
- Keine zusätzliche permanente Netzwerk-Task; vorhandener lokaler Transfer-Worker.
- Unterbrochene HTTP-Übertragung aktiviert keinen neuen Boot-Slot. Ein abgeschlossener
  Upload ist noch kein erfolgreicher Firmware-Boot; P4-Health-Check/Rollback bleibt aktiv.
- **Kein generischer C6-Firmware-Installer**: Abweichende Versionen benötigen
  eine bewusst gepflegte Whitelist aus erwarteter Größe und vertrauenswürdigem Hash.
- **Kein automatischer P4-Online-Updatefeed**: eine verbindliche signierte Quelle ist
  im Projekt nicht konfiguriert. „Abfragen“ liest die vorhandenen SD/USB-Dateien.
- Nach Erstellung zunächst lokal kompilieren und auf echter Hardware prüfen.
