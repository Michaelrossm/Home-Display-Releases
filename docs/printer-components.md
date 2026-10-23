# Gemeinsame Drucker-Schnittstelle – Schema 2

Lokale Erweiterung der vorhandenen Firmware, ohne neue Tasks, Server, Bibliotheken,
Partitionsänderungen oder zweite Profilverwaltung. Die Kalender-Arbeiten im
Workspace bleiben erhalten; deren noch offene Cloud-Funktionen werden durch diesen
Drucker-Auftrag nicht abgeschlossen. Keine App-Dateien, Commits oder Uploads.

## Architektur und Dateien

`PrinterService` besitzt weiterhin Planung, Cache, Profile und Befehlswarteschlange.
`NetworkWorker` führt HTTP aus. Lua und Port-80-Web-Einstellungen lesen denselben
Cache. `printer_profiles` bleibt die einzige appbezogene Profilquelle, maximal fünf
Profile pro Owner, maximal zwei aktive Owner. Secrets bleiben owner-scoped im
bestehenden Secret-Speicher. Der ausdrücklich gewählte Standard `reprap` bleibt.

Geändert: `PrinterService.{h,cpp}`, `printer/PrinterSafety.h`, `PrinterAdapter.h`,
`RepRapPrinterAdapter.cpp`, `MoonrakerPrinterAdapter.{h,cpp}`,
`OctoPrintPrinterAdapter.cpp`, `PrinterWebProfiles.h`, `PrinterJson.h`,
`apps/lua/PrinterLuaBridge.cpp`, `LuaJsonBridge.{h,cpp}`,
`AppWebSettingsService.cpp` und die vorhandene Web-Seite in `WebServerService.cpp`.
Neu: `printer/PrinterComponents.h`, gemeinsame begrenzte Komponenten-Helfer.
Tests: `test_printer_components.py`, vorhandene Drucker-/Web-Tests und deren
Transport-Fixtures sowie der echte Lua-Runtime-Hosttest.

## Datenvertrag

`hp_printer_capabilities().schema_version == 2`, `components == true`.
Bestehende `hp_printer_*`-Funktionen bleiben registriert. `snapshot(id)` ergänzt:

- `temperatures`, `heaters`, `fans`, `axes`, `tools`, `extruders`;
- `job`, `motion`, `overrides`, `storage`, `connection`, `diagnostics`;
- `schema_version`, `availability`, `truncated`, `updated_at`, `firmware`.

Alte Felder einschließlich `nozzle`, `bed`, `filename`, `progress`, `duration`,
`remaining`, `layer`, `layers` und `speed_factor` bleiben erhalten. Unbekannte
numerische Werte sind jetzt korrekt **JSON null**, nicht erfundene Nullwerte.
Lua erhält hierfür `hp_json_null`; vor Rechnen/Formatieren `type(value)=='number'`
prüfen. Bereits vorhandene Apps mit unbedingtem Rechnen auf fehlenden Werten müssen
diese fehlenden Werte behandeln. Unbekannte neue Felder können ignoriert werden.

Komponenten haben stabile IDs, Namen, Kategorie/Einheit und gegebenenfalls
Steuerbarkeit. `valid` kennzeichnet Temperaturmessungen; `controllable` ist eine
physische Fähigkeit, keine Freigabe für jede Betriebsphase. Jeder Steuerbefehl wird
mit frischem Status erneut geprüft. `updated_at` gilt für alle Werte dieses atomaren
Snapshots; bei `stale`/Offline ist diese Zeit wichtiger als die letzte Abfragezeit.
Sammlungen unterscheiden `not_loaded`, `available`, `unavailable`, `unsupported`
und `absent`. `revision`/`printer.status` ändern sich nur bei inhaltlichen Änderungen,
nicht wegen Zeitstempel oder HTTP-Zähler.

UI-Empfehlung: Widgets einmal pro Komponenten-ID erstellen, nur geänderte Werte
setzen, unbekannte Werte als „—“ anzeigen, Listen scrollbar halten. Keine eigene
HTTP-Schleife und keine komplette Neuerstellung einer Seite pro Snapshot.

## Adapter und Grenzen

RRF: Standalone `rr_connect`/`rr_model`, SBC `machine/connect`/`machine/model`.
Analog-Sensoren sind kanonisch; Heizungs-/Tool-/Bett-/Kammer-Referenzen erzeugen
keine zweite identische Temperaturkarte. Nicht-Temperatur-Kanäle wie Feuchte,
Druck und Strom werden nicht als °C ausgegeben. Digitale Kanäle werden nur bei
einem ausdrücklich vorhandenen Temperaturfeld aufgenommen. Sensorfehler bleiben
unverfügbar. Alte Heizungslisten und neue gruppierte Mappings werden berücksichtigt.
Mehrdeutige Mehrheizer-Steuerung bleibt gesperrt. Generische Heizungs-IDs werden
nur über nachgewiesene einzelne Bett-/Kammer-/Tool-Zuordnungen gesteuert; keine
Heater-ID wird als Tool-ID geraten. Session-Erneuerung ist begrenzt; Befehle werden
bei unklarem Ausgang niemals automatisch wiederholt. Keine zyklischen Status-G-Codes.

Moonraker: Objekterkennung beim ersten Poll, nach Fehler/Wiederverbindung,
veränderter Heizungs-/Sensor-Inventarliste, zurückgesetzter Eventzeit und spätestens
nach fünf Minuten. Dazwischen eine gebündelte Statusabfrage, keine Objektliste pro
Poll. Mehrere Extruder, generische Heizungen, Temperatur-Sensoren/-Hosts/-Fans und
normale/generische/automatische Lüfter werden gelesen. Reine Sensoren und
Temperaturlüfter werden nicht als steuerbare Heizungen ausgegeben. Generische
Heizungen und manuelle Lüfter sind über ihre tatsächlich erkannte ID steuerbar;
mehrdeutige oder G-Code-unsichere Namen werden abgelehnt.

OctoPrint: alle vorhandenen Standardkanäle `toolN`, `bed`, `chamber` aus der
Standard-API. Keine erfundenen Plugin-Endpunkte. Status, Auftrag, Dateien,
Home, Pause/Fortsetzen/Abbrechen und Feedrate bleiben unterstützt. Autoritative
Achsenpositionen/Homing-Grenzen, Lüfter und Heizungs-Maxima fehlen in diesen
Endpunkten: Jogging/Heizen bleiben ausdrücklich gesperrt. Home verwendet weiterhin
die Standard-API für X/Y/Z. Layer-/Tool-Aktivitätsinformationen werden nicht geraten.

Typed Control bleibt `hp_printer_command(id, kind, parameters)`, beispielsweise
`set_temperature` mit `heater='heater_1'` (RRF) bzw. `heater='extruder1'` oder
`heater='heater_generic chamber'` (Moonraker). Alte `heater='nozzle'/'bed'` bleiben.
Moonraker-Lüfter können `fan_id='fan_generic auxiliary'` verwenden. Der bestehende
Standardlüfter bleibt über Index 0 erreichbar. Kein freier Bewegungs-G-Code.
Nur ein ausstehender Steuerauftrag pro Profil; aufgegebene Reservierungen werden
erst nach Ablauf und vollständig geleerter gemeinsamer Worker-Queue freigegeben.
Ein Owner-Wechsel verwirft fremde Sessions/Caches, auch bei identischer LAN-Adresse.

## Planung und Ressourcen

Zielintervalle bleiben 2 s im Vordergrund und 15 s im Hintergrund. Hintergrund
mit hoher Vordergrundlast wird verschoben; Offline/Tablet-Sperre und deaktivierte
Profile verursachen keine Drucker-HTTP-Abfragen. Exponentieller Fehler-Backoff
5/10/20/40 s, begrenzt. Ein gemeinsamer Worker, keine Echtzeitgarantie bei fünf
Druckern/anderen Jobs. Eine Abfragephase bleibt auf 4 s, ein Steuerjob auf 6 s
begrenzt; einzelne Requests maximal 1,5 s.

Obergrenzen pro Snapshot: 24 Temperaturen, 16 Heizungen, 16 Lüfter, 12 Achsen,
8 Tools mit je 8 Zuordnungen, 8 Extruder. Zu große Listen werden kenntlich
abgeschnitten, zu große/ungültige HTTP-/JSON-Antworten abgelehnt. Maximal 64 KiB
HTTP-Antwort und 64 KiB Parserquota, JSON-Tiefe 16 (OctoPrint 12), Snapshot <32 KiB.
Die native Printer-Konvertierung darf höchstens 2048 Lua-JSON-Elemente ausgeben;
die öffentliche `hp_json_*`-API behält standardmäßig 512 Elemente und 32 KiB.
Ein explizites `max_elements`-Budget erlaubt bis zu 2048 Elemente, ohne die
Byte-, Tiefen- oder Parser-Speichergrenzen anzuheben.

HTTP pro Statuszyklus (ohne ausdrücklich angeforderte Dateien/Makros):

| Adapter | Normal | Erstkontakt/Erneuerung |
| --- | ---: | ---: |
| RRF Standalone | 1 | 2; ggf. zusätzliche geänderte Reply-Abholung |
| RRF SBC, ausdrücklich konfiguriert | 1 | 2; Auto-Erkennung ggf. 3 |
| Moonraker | 1 | 2 mit Objekterkennung |
| OctoPrint | 2 | 2; Offline-Drucker 1 |

Ein abgelaufenes RRF-Session-Timeout kann im 15-s-Hintergrundintervall jedes Mal
eine Anmeldung erfordern. `diagnostics.http_requests`, `last_poll_requests` und
`last_poll_ms` liefern tatsächliche Worker-Zähler; keine behaupteten LAN-Messungen.

P4-GDB aus dem Build: `sizeof(PrinterStatus) == 12304` Byte. Statuspuffer werden
gezielt in PSRAM angelegt: 61.520 B für fünf Profile bzw. höchstens 123.040 B für
beide Owner, zusätzlich zeitweilige Parser-/Transport-/Snapshot-/Lua-Puffer.
Keine großen Statuskopien auf dem 12-KiB-Worker-Stack. Ein RRF-Poll benötigt
zeitweise zwei weitere Statuspuffer, insgesamt 24.608 B. Keine zusätzlichen Tasks.
Live-Heap-Höchststand, Fragmentierung und Display-Reaktionszeit sind noch nicht
auf echter Hardware gemessen; diese Zahlen sind keine Hardware-Laufzeitmessung.

Build-Ausgang dieses Auftrags: Flash 5.101.560 B, statisches RAM 128.032 B.
Endbuild `home_display_p4_r3_dev`: Flash 5.131.470 B (+29.910 B), statisches RAM
114.832 B (-13.200 B). Statisches RAM wurde unter anderem durch Verschieben der
größeren Statuspuffer nach PSRAM freigegeben; dies ist keine Senkung des gesamten
dynamischen Speicherbedarfs. Build erfolgreich. 187 Regressionstests plus die
Architekturprüfungen bestanden. Die Release-Validierung besteht die Tests, meldet
aber die bewusst vorhandenen Standardpasswörter in `RecoveryWebService.cpp` und
`WebServerService.cpp` als mögliche Secrets und ist deshalb nicht vollständig grün.

## Verifikation und Quellen

Hosttests führen produktive Adapter/Serializer/Scheduler/Sicherheitsregeln mit
Fake-Transport aus: Standalone/SBC, mehrere Sensoren/Heizungen, Fehler/null,
Duplikat-Zuordnung, Limit/Trunkierung, geänderte Moonraker-Objekte, Mehrkanal-
OctoPrint, fünf Profile, Deaktivierung, Offline, Backoff, Profile-/Owner-Wechsel,
konkurrierende Befehle und Cache-/Legacy-Felder. Der echte Lua-State testet die
begrenzte native Konvertierung und weiterhin abgewiesene öffentliche Groß-JSONs.
Keine Behauptung, echte Duet-/Klipper-/OctoPrint-Geräte seien hier getestet worden.

Protokollgrundlagen: [Duet Object Model](https://github.com/Duet3D/RepRapFirmware/wiki/Object-Model-Documentation),
[Moonraker Printer API](https://moonraker.readthedocs.io/en/latest/external_api/printer/),
[OctoPrint Printer API](https://docs.octoprint.org/en/main/api/printer.html).
Vor Installation passende P4-Revision wählen und Profile sichern. Nichts geflasht.
