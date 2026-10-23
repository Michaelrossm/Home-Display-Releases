# Home-Display: Hardening vom 02.10.2026

Umfang: gezielte Fehlerbehebung im vorhandenen, bereits geänderten Projekt.
Dies ist keine Zusicherung, dass die gesamte Firmware fehlerfrei ist.
Die gepinnten Bibliotheksversionen und Board-Pins bleiben unverändert.

## Änderungen

- Revisionsprofile wieder getrennt: r3 nutzt das 400-MHz-Board und
  Silicon-Minimum 300, Legacy weiterhin 360 MHz / Minimum 100. CMake wählt
  die Defaults auch ohne exportierte PIOENV aus dem SDKCONFIG-Dateinamen.
  Der Entwickler-Build prüft zusätzlich die erzeugte Silicon-Konfiguration.
- Nicht benötigte LVGL-Beispiele und Demo-Konfigurationen ausgeschaltet.
- Vor automatischem Erstformatieren wird die **gesamte** AppFS-Partition auf
  gelöschte Bytes geprüft. Ein leerer Header oder Lesefehler reicht nicht aus.
- Speicherpfade weisen Punktsegmente, Backslashes und Steuerzeichen ab;
  auch StorageClass-Pfade nutzen die zentrale Prüfung. Atomare Schreibvorgänge
  weisen einen Null-Datenzeiger bei positiver Länge ab.
- HTTP-Dateiuploads sichern das alte Ziel vor dem Commit und stellen es bei
  fehlgeschlagenem Umbenennen wieder her. Falls auch die Wiederherstellung
  fehlschlägt, bleibt die Sicherung erhalten. Eine vorhandene Sicherung eines
  unterbrochenen Uploads wird beim nächsten Commit berücksichtigt.
- Webverwaltung setzt `Passwort` nur bei fehlendem Passwort. Ein Update
  überschreibt keine vorhandenen Zugangsdaten und druckt sie nicht ins Log.
  HTTP Basic bleibt unverschlüsselt: nur im vertrauenswürdigen LAN verwenden.
- Netzwerkreset berücksichtigt alle drei Profile, Adressparameter,
  Zeitplanparameter und gespeicherten WLAN-Passwörter.
- Zentrale Secret-Schreibmethoden beachten die Schreibsperre bei unbekanntem
  oder neuerem Konfigurationsschema.
- Netzwerk-Worker und Background-Scheduler räumen fehlgeschlagene Starts auf.
  Diagnosezähler sind atomar; der Scheduler verwirft keine Job-Freigabe wegen
  eines kurzen Mutex-Timeouts mehr. Seine Startmeldung nennt die reale Workerzahl.
- WLAN-Sendeleistung wird nur bei Änderung geschrieben. Fehlgeschlagene
  Energiespareinstellungen werden nicht als übernommen gecacht. Radio-Neustarts
  invalidieren die entsprechenden Caches; ein nicht gespeicherter WLAN-Schalter
  wird nicht stillschweigend nur im RAM geändert.
- URL-Diagnosen entfernen auch Fragmente. SD-Zugriffe der Webverwaltung gehen
  über den zentralen StorageService.
- Tests und versionsgebundene SDKCONFIG-Overlays werden nicht mehr pauschal
  durch `.gitignore` ausgeschlossen.
  Bestehende, formatierungsabhängige Tests wurden an die tatsächlichen
  Schutzfunktionen angepasst. Build-Hygiene prüft getrackte statt lediglich
  vorhandene generierte Dateien.

## Prüfung

- `python tools/dev.py doctor`: bestanden.
- `python tools/dev.py test`: 66 Tests und vier Architekturprüfungen bestanden.
- `python tools/dev.py validate`: Quellcode-Validierung bestanden.
- Ein nativer C++-Test kompiliert die tatsächlichen Produktionsfunktionen für
  Upload-Commit, Pfadprüfung und Blank-Partition-Prüfung gegen Test-Doubles.
  Er prüft fehlgeschlagene Renames, eine unterbrochene Sicherung, ungültige
  Pfade, einen beschädigten Partition-Endbereich und einen Lesefehler.
- `python tools/dev.py build`: r3-Entwicklerbuild erfolgreich, einschließlich
  Bootloader, Firmware und Factory-Binärdatei. Generierte Silicon-Konfiguration
  ist Minimum 300. Der erste Lauf wurde
  durch gleichzeitige VS-Code/PlatformIO-Konfiguration gestört. Eine alte
  generierte r3-SDKCONFIG wurde als
  `sdkconfig.home_display_p4_r3_dev.before-hardening` gesichert und neu erzeugt.
  Auch das generierte `memory.ld` und seine vorverarbeitete Vorlage stammten
  noch aus der alten Silicon-Konfiguration; sie wurden gesichert und neu erzeugt.
- Verbleibende Größenwarnung: PlatformIO meldet 334.548 Byte RAM-Daten gegenüber
  327.680 Byte im Board-Metadatensatz. Der revisionsabhängige Linker akzeptiert
  das Image. Der Metadatenwert wurde nicht angehoben, um diese Warnung zu
  verstecken. Tatsächliche freie Heap-/Stack-Reserven sind am Gerät zu messen.
  Firmware-Flashbelegung laut Builder: 3.143.156 Byte / 11.534.336 Byte OTA-Slot.

## Grenzen und Hardware-Abnahme

Kein Gerät geflasht, keine gespeicherten Gerätedaten verändert. Ein Compilerlauf
ist keine Hardware-Abnahme. FAT/LittleFS-Stromausfallsicherheit wird durch den
Mock-Dateisystemtest nicht bewiesen. Die Upload-Sicherung ist kein universelles
transaktionales Dateisystem und benötigt zusätzlich Platz für das alte Ziel.

Am Gerät noch prüfen: mehrstündiger LAN/WLAN-Wechsel, WLAN-Zeitplan, SD voll / SD
entfernt, Audiofokus, App-Installation während Netzwerksuche sowie signiertes OTA
einschließlich Stromausfall und Rollback. Die r3-Binärdatei darf nicht auf das
ältere P4-Silicon geflasht werden. Ohne bekannte Revision nicht flashen.

Es wurde keine Vorher-/Nachher-Leistungsmessung auf Hardware durchgeführt.
Weitere Module sind damit nicht vollständig auditiert.
