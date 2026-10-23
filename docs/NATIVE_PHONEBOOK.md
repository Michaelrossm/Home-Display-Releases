# Native Telefonbuch-App

Die fest integrierte App `phonebook` ist im Launcher sichtbar und verwendet den
vorhandenen `PhonebookService`, `StorageService` und `NetworkWorker`.
Keine App-Tasks, keine zweite Netzwerkverwaltung und keine automatische
Aktivierung von Cloud-Kontaktberechtigungen.

## Funktionen

- Name, E-Mail, Telefon, Mobil, Straße, Hausnummer, PLZ, Ort, Land und Notizen.
- Bis zu 128 lokale Kontakte, bis zu 16 Benutzerprofile.
- Benutzer anlegen und umbenennen; Zuordnung über unveränderliche IDs.
- Eine gemeinsame lokale Kontaktdatei; Benutzer sind ausschließlich Filter,
  kein Login, keine Benutzerrechte und keine getrennten Datenbanken.
- Suche über Name, E-Mail und Telefonnummern; maximal 20 Kontaktzeilen je Seite.
- Bearbeiten und zweistufig bestätigtes Löschen; atomare Speicherung und
  Wiederherstellung gültiger Backup-Dateien. Beschädigte Daten niemals formatieren.
- Einstellungen-PIN schützt Schreibvorgänge. Tablet-PIN schützt Datenzugriffe.
- Worker enthält keine LVGL-Aufrufe. Ergebnisse werden im UI-Timer verarbeitet;
  Generationen verwerfen veraltete Completions. Wiederöffnen während eines Jobs
  fordert danach einmalig ein neues Laden an, ohne Busy-Loop.

Lokale Daten: `UserData:/accounts/phonebook_local.json`, Version 1,
`kind: local`, Arrays `users` und `contacts`, optionales Kontaktfeld `userId`.
IDs werden im zentralen Dienst erzeugt und auf Kollisionen geprüft.
Maximal 64 KiB je Dokument; Feldgrenzen werden in UTF-8-Bytes geprüft.
Normale Kontakte sind keine ausführbaren Links; kein automatischer Anruf.

Bestehende per-Account- und Read-only-Provider-Telefonbücher bleiben unverändert
getrennt vom lokalen Dokument und weiterhin opt-in. Die neue App zeigt derzeit
lokale Kontakte; Cloud-Import, Export, Mehrfachzuordnung und Benutzerlöschung
sind nicht implementiert. Nicht als kontenübergreifende Cloud-Ansicht ausgeben.

## App-eigene Web-Konfiguration

Lua-Pakete verwenden weiterhin `web_settings.json` (Schema 1) auf dem vorhandenen
authentifizierten Webserver. Display- und Web-Konfiguration verwenden dieselben
owner-scoped KV-/Secret-Schlüssel. Keine eigenen Listening-Sockets/Server-Tasks
und kein app-geliefertes HTML/JavaScript.

Neue allgemeine Lua-APIs:

- `hp_web_settings_capabilities()` gibt Version, Schema, gemeinsamen Server,
  KV- und CSRF-Unterstützung sowie `custom_html=false` zurück.
- `hp_web_settings_url()` liefert die LAN-Adresse zur eigenen Einstellungsseite
  (`/?app_settings=<eigene App-ID>`); ohne IP `nil, network_offline`.

Die URL enthält keine Zugangsdaten und umgeht weder Anmeldung noch CSRF.
Sie ist eine Adresse, kein Nachweis, dass der Webserver gerade aktiviert ist.
Die Seite lädt zuerst den normalen Web-Kontext und öffnet anschließend die
gewählte App-Konfiguration. Andere App-IDs werden nicht aus Lua übernommen.

## Installationshärtung

Port 80 und 8088 serialisieren die vollständigen Lua-Upload-/Commit-/Reset-HTTP-
Operationen über dieselbe nichtblockierende Mutex. Parallelversuche erhalten
503 `installer_busy`. Die UI-Aktivierung benötigt diese HTTP-Mutex nicht.
So kann ein zweiter HTTP-Handler das globale Ziel-/USB-FS-Objekt nicht während
einer laufenden Übertragung oder einer wartenden Aktivierung ersetzen.

Web-Schema-Zugriffe und `resetStage` verwenden die vorhandene Paket-I/O-Mutex
als Read-Guard. Read-Guards invalidieren keine Scanner-Generation.
Web-Einstellungen dereferenzieren nicht mehr den möglicherweise veralteten
`AppRegistryEntry.app`-Pointer, sondern prüfen das aktuelle Registry-Objekt
unter diesem Guard. Der Guard verhindert dessen paralleles Löschen.

Beide Risiken sind im Code nachgewiesen und behoben. Der USB-Mitschnitt erfasste
einen Installationsplan für `radio`, aber keinen Panic-/Reset-Backtrace.
Der konkrete gemeldete Geräteabsturz ist daher noch nicht hardwareseitig
zugeordnet oder nach dem Fix bestätigt. Neue Installationsphasen-Logs helfen
beim nächsten gezielten Test; sie enthalten keine Zugangsdaten.

Tests führen tatsächliche Filter-, Validierungs-, CRUD- und Completion-Logik mit
begrenzten Host-Stubs aus. Sie ersetzen keine LVGL-Touch- oder Hardwareprüfung.

## Lokale Prüfung vor der Kontaktfeld-Erweiterung

- 278 Hosttests und Architekturprüfungen erfolgreich; Release-Validator VALID.
- `home_display_p4_legacy_dev` (360 MHz): Build erfolgreich.
- Statischer RAM: 56.168 Bytes; Firmware-Flash: 5.660.906 Bytes.
  Diese Buildwerte sind keine Messung des dynamischen Heap-/PSRAM-Verbrauchs.
- Kein Flash, Commit oder Push. Weitere Builds ausschließlich Legacy/360 MHz
  entsprechend dem angeschlossenen P4-v1.3-Gerät.

## Erweiterte Kontaktfelder und Geburtstage

Pro Kontakt sind bis zu acht Telefonnummern, acht E-Mail-Adressen und vier
Adressen möglich. Bezeichnungen sind frei bearbeitbar; die Auswahl bietet
Privat, Arbeit, Mobil, Zentrale, Fax und Sonstige. Adressen enthalten Straße,
Hausnummer, Postleitzahl, Ort, Region und Land. Firma, Berufsbezeichnung,
Notizen und Geburtstag gehören ebenfalls zum zentralen Kontaktmodell.

Alte JSON-Kontakte bleiben lesbar. Die bisherigen einzelnen Felder werden nur
bei fehlenden neuen Arrays übernommen. Neue Bearbeitungen aktualisieren die
alten Vorschaufelder; keine zweite Kontaktdatenbank wird angelegt. Die Grenzen
128 Kontakte, 16 Benutzer und 64 KiB je Telefonbuchdatei bleiben bestehen.

Geburtstag: `JJJJ-MM-TT`, alternativ `--MM-TT` ohne bekanntes Geburtsjahr.
Das bestehende Datumssystem unterstützt 1900 bis 2199. Lokale Kontaktgeburtstage
werden im vorhandenen virtuellen Geburtstagskalender als schreibgeschützte
Ganztagseinträge angezeigt. Kein Kopieren in die Terminablage, keine zusätzliche
Task. Kontaktänderung/-löschung invalidiert auch Kalender-Cursor und Ansicht.
Der Kalenderfilter und die Sichtbarkeit des Geburtstagskalenders gelten weiter.
Am 29. Februar Geborene erscheinen in Nicht-Schaltjahren am 28. Februar.
Gleiche E-Mail und Monat/Tag verhindern doppelte Einträge zu Kalender-Personen.
Kontakt-Benutzerfilter sind weiterhin keine Kalender-Personen oder Anmeldung.

## Standard-Dateiaustausch

Der vorhandene zentrale `ContactProtocol` liest UTF-8-vCard 3.0 und 4.0 und
schreibt vCard 4.0: FN, UID, TEL, EMAIL, ADR, ORG, TITLE, NOTE und BDAY.
Mehrfachwerte, TYPE sowie gruppierte Apple-X-ABLabel-Bezeichnungen bleiben
erhalten. Zeilen werden bytebegrenzt und UTF-8-sicher gefaltet. ADR hat keinen
eigenen Hausnummernslot: beim Export werden Straße und Hausnummer kombiniert;
ein Import übernimmt diese gemeinsame Zeile als Straße, ohne Nummern zu raten.

Am Display: „vCard importieren“ bzw. „vCard exportieren“ am Kontakt. Austausch
über `data:/contacts/` in der vorhandenen Dateien-App; Dateinamen ohne Ordner.
Eine Person pro `.vcf`, maximal 16 KiB. Import erstellt eine neue lokale ID und
übernimmt den gewählten Benutzerfilter; fremde UID überschreiben nichts.
Export überschreibt keine vorhandenen Dateien. Beides läuft PIN-geschützt im
bestehenden Worker über StorageService; keine freien System-/Secret-Pfade.
vCard 2.1, Quoted-Printable/Base64-Felder, Kontaktfotos, CSV, Sammel-vCards und
beliebige Erweiterungsfelder werden nicht als vollständig unterstützt behauptet.

CardDAV sowie Google/Microsoft-Kontaktsnapshots übernehmen auch die neuen
Mehrfachfelder und Geburtstage innerhalb der Grenzen. Cloud-Kontakte bleiben
read-only und ausdrücklich opt-in; die native Telefonbuchansicht und der
automatische Kontaktgeburtstagskalender verwenden derzeit lokale Kontakte.
Keine automatische Kontaktfreigabe und kein Cloud-Schreiben hinzugefügt.

Referenzen:
- https://www.rfc-editor.org/rfc/rfc6350
- https://www.rfc-editor.org/rfc/rfc2426
- https://developers.google.com/people/api/rest/v1/people
- https://learn.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0

Abschließende Prüfung der Kontaktfeld-Erweiterung: 284 Hosttests plus
Architekturprüfungen erfolgreich, Release-Validator VALID und ausschließlich
Legacy-360-MHz-Build erfolgreich. Statischer RAM 56.168 Bytes, Firmware-Flash
5.711.382 Bytes (50.476 Bytes mehr als vor dieser Erweiterung); dies sind
Buildwerte, keine dynamische Heap- oder Hardware-Performance-Messung.
Kein Flash, Commit oder GitHub-Push. Displaybedienung und Cloud-Synchronisation
mit diesen neuen Feldern sind noch nicht am angeschlossenen Gerät geprüft.
