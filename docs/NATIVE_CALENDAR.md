# Nativer Kalender

## Integration

`CalendarApp` ist eine fest registrierte `IApp` mit der vorhandenen ID `calendar`.
Sie verwendet `CalendarService`, `CalendarEventStore`, `CalendarSyncService`,
`CalendarApi`, `StorageService` und die zentrale Konten-/PIN-Verwaltung.
Es gibt keine zweite Kalenderdatenbank, keinen zweiten OAuth-Client und keine
neuen permanenten FreeRTOS-Tasks. Vorhandene Lua-Bridges bleiben registriert.

Die native Registrierung erfolgt vor dem Lua-Paket-Scan. Ein installiertes
Lua-Paket mit gleicher ID wird nicht zusätzlich registriert. Seine Dateien,
KV-Einstellungen und bisherigen Kalenderdaten werden nicht gelöscht. Unbekannte
app-eigene Datenformate werden nicht stillschweigend als Firmware-Daten importiert.

## Oberfläche und Ressourcen

- Monat: ein gezeichnetes 42-Zellen-Raster, Montag zuerst, keine 42 Buttons.
  Tageszahlen stehen vor dem ersten Storage-/Netzwerkergebnis bereit.
  Auswahl und Markierungen invalidieren nur geänderte Zellen.
- Woche/Tag: gezeichnetes Stundenraster, farbige Überlappungsspalten, Ganztagstermine,
  aktuelle Zeit; nur der innere Zeitbereich scrollt.
- Agenda: Suchfeld und zwölf wiederverwendete Zeilen mit Seitennavigation.
- Drei lokale Bereichscaches; Abfragen mit 32 Ergebnissen pro Seite, maximal
  256 sichtbaren Vorkommen. Begrenzte Ergebnisse werden angezeigt, nicht versteckt.
- Root-Größe relativ zum bestehenden App-Host; keine zweite Bildschirmrotation.
- Ein LVGL-Timer je geöffneter Seite; keine eigenen LVGL-Handler/Tasks.
  Beim Schließen werden Timer, Objekte und owner-scoped Jobs aufgeräumt.

`CalendarController` startet alle Dateizugriffe über den vorhandenen
`LuaJobService` mit reserviertem Firmware-Owner `@calendar-native`. Der Name
bedeutet nicht, dass Lua ausgeführt wird: der Handler ist natives C++.
Lange Kalenderabfragen benutzen den vorhandenen Transferworker, haben ein
10-Sekunden-Limit und prüfen kooperativ den owner-scoped Abbruchmarker.
Bereits laufende synchrone Dateisystem-/HTTP-Aufrufe werden nicht als hart
unterbrechbar ausgegeben. UI/LVGL wird nicht aus dem Worker aufgerufen.

## Bearbeitung, Personen und Einstellungen

Lokale Termine unterstützen Titel, Kalender, Beginn/exklusives Ende, Ganztag,
Ort, Straße/Hausnummer/PLZ/Stadt/Region/Land, Telefonnummer, Notizen,
Teilnehmer, Wiederholungen und erhaltene Ausnahmen. Konflikte werden über
die bestehende Revision geprüft. Serien oder einzelne Vorkommen lassen sich
mit Bestätigung löschen. Cloud- und virtuelle Termine bleiben schreibgeschützt.

Bis zu 16 Personen und 32 Kalender gemäß bestehenden Speichergrenzen;
gemeinsame Familien-/Haushaltskalender, Farben und gespeicherte Filter.
Sichtbarkeit ändert keine Kontenberechtigung und beendet keine Synchronisierung.
Private Schreibrechte verlangen weiterhin den vorhandenen Settings-PIN.
Geburtstage ohne Geburtsjahr und Bundesland-/Gemeindefeiertage werden aus der
vorhandenen Engine erzeugt und erhalten eine schreibgeschützte Detailansicht.

Lokale Erinnerungen verwenden denselben atomaren Termin-/Regelspeicher.
Der vorhandene Netzwerkworker prüft lokale Erinnerungen alle 15 Sekunden mit
einem 2-Sekunden-Scanbudget, ohne Netzwerkaktivität anzufordern. Ein Cache für
den nächsten Fälligkeitszeitpunkt vermeidet unveränderte Speicherscans für bis
zu fünf Minuten; Datenänderungen und anstehende Erinnerungen verkürzen dies.
Eine native, deckende Meldung funktioniert auch bei geschlossener
App, ohne Navigation oder Audio-Fokus zu übernehmen. Unter Sperre wird kein
Termintitel angezeigt. Grenzen: acht anstehende Meldungen, 32 bestätigte
Vorkommen im RAM, zwei Minuten Nachholfenster, keine Zustellung bei ausgeschaltetem
Gerät, keine Neustart-persistente Bestätigungs-Historie und kein akustischer Alarm.
Cloud-Erinnerungen und Kontakt-Geburtstage werden nicht als unterstützt gemeldet.

## Online-Kalender

### Benutzer und Kontenzuordnung

Bis zu 16 Kalender-Benutzer können unter den Kalender-Einstellungen auf Display
und Web angelegt werden, auch nur mit Namen (ohne E-Mail). Dafür wird eine stabile
interne Personenidentität vergeben; bestehende Personen bleiben unverändert.
Ein bereits angelegter Benutzer wird über seine ID bearbeitet, nicht dupliziert.

Unter „Online-Kalender auswählen“ wird ein vorhandenes zentrales Kalenderkonto
einem Benutzer zugeordnet oder als gemeinsam markiert. Mehrere der bis zu fünf
Konten können demselben Benutzer gehören. Google, Microsoft, iCloud und andere
CalDAV-Konten verwenden unverändert die zentrale Konten-/Synchronisierungslogik.
Ein reines Mailkonto muss zunächst dort für Kalender eingerichtet werden;
diese Zuordnung aktiviert weder OAuth noch automatische Kontaktimporte.

Die Zuordnung wird über ConfigService gespeichert, mit Konto-E-Mail und
Provider/CalDAV-Server abgeglichen. Bei Kontoaustausch wird eine unpassende alte
Zuordnung nicht übernommen. Schreibfehler lassen die vorherige Zuordnung bestehen.
Der Personenfilter berücksichtigt eigene lokale und zugeordnete Online-Termine
sowie lokale Teilnehmerzuordnungen. „Mein Kalender“ schließt den zugeordneten
Online-Kalender ein. Benutzer sind Filter, keine getrennten Anmeldungen oder
Zugriffsrechte; Kontozuordnungen benötigen weiterhin die Einstellungsfreigabe.

Google und Microsoft lesen Kalenderlisten und pro Kalender expandierte,
paginierte Zeitraumabfragen. Google benötigt für die Liste bei einer erneuten
Autorisierung zusätzlich `calendar.calendarlist.readonly`. Alte Freigaben für
den Standardkalender bleiben verwendbar; ein verweigerter Listenabruf löscht
keine bestehende Liste und ersetzt keine Anmeldedaten.

CalDAV/iCloud verwenden den vorhandenen Discovery-Connector und RFC-4791-REPORT
mit serverseitiger Expansion. Nicht unterstützte TZID/floating/uneexpandierte
Serien, fehlerhafte oder teilweise Antworten werden abgelehnt; der vorherige
Cache bleibt erhalten. Es wird keine Kalenderliste als vollständiger
Terminabruf ausgegeben.

Die native Einstellungsseite kann bis zu 16 bekannte Sammlungen je Konto auswählen.
Die Auswahl ist an Provider/Server und Kontoinhaber gebunden; keine fremden IDs
oder Zugangsdaten werden übernommen. NVS-Schlüssel `cal_sel_0` bis `cal_sel_4`
sind kürzer als 16 Zeichen, Einstellungen `cal_native_v1` ebenfalls.
Die Auswahl erfordert den vorhandenen Settings-PIN und wird nicht während eines
laufenden Kontenjobs geändert. Die UI erhält keine Tokens oder Passwörter.

Grenzen bleiben explizit: maximal 128 Cloud-Termine pro Konto, 15 Sekunden pro
OAuth-Zeitraumabruf, 20 Sekunden gemeinsames CalDAV-Budget. Online-Sammlungen
werden im vorhandenen Konto-Kalender zusammengeführt; individuelle Remote-
Sammlungsfarben sind noch nicht als getrennte lokale Kalender registriert.
Cloud-Schreibzugriffe sind aufgrund vorhandener Read-only-Freigaben deaktiviert.

Protokollreferenzen:
- https://www.rfc-editor.org/rfc/rfc4791
- https://developers.google.com/workspace/calendar/api/v3/reference/calendarList/list
- https://learn.microsoft.com/en-us/graph/api/calendar-list-calendarview?view=graph-rest-1.0

## Verifikation

Hosttests prüfen Kalendergeometrie, Navigation, Überlappungen, ICS-Import,
Erinnerungszustellung/Deduplizierung/Privatsphäre sowie echte Provider-
Abfragemethoden bei Paginierung, Abbruch, RAM-Mangel, Offline-Zustand und
unveränderten Caches. Der P4-R3-Build prüft Kompilierung und Linken, nicht Touch,
reale Konten, flüssige Darstellung oder Erinnerungsgenauigkeit auf Hardware.
Keine gemessenen Hardware-FPS/Latenzen ohne Gerätetest behaupten.

Verifizierter lokaler Stand am 11.10.2026:

| R3-Entwicklungsbuild | Statisches RAM | Firmware-Flash |
| --- | ---: | ---: |
| Geprüfter Sprechanlagen-Teilstand | 127.896 Byte | 5.445.644 Byte |
| Mit nativem Kalender | 129.352 Byte | 5.569.688 Byte |
| Kalender-Differenz | +1.456 Byte | +124.044 Byte |

Das ist der Linkerbericht, keine Messung des dynamischen PSRAM-/LVGL-Verbrauchs.
`home_display_p4_r3_dev` (400 MHz) wurde erfolgreich gebaut. Validator:
`VALID Home-Display 0.3.3b`, 253 Hosttests erfolgreich. Zusätzliche dauerhafte
FreeRTOS-Tasks: 0. Keine Firmware geflasht, keine GitHub-Änderung vorgenommen.

### Spätere Editor-Korrektur am 11.10.2026

Der Termin-Editor liegt jetzt außerhalb des Root-Flexlayouts als Overlay;
Formularspalten haben Inhaltshöhe und scrollen gemeinsam. Notizen besitzen ein
größeres Eingabefeld. Personen erscheinen als lokale Teilnehmer; ohne gespeicherte
Personen wird die Personenverwaltung erklärt. Keine automatischen E-Mail-Einladungen.
Bei offenem Keyboard wird der sichtbare Editorbereich verkleinert. Außen-Tippen,
Fertig und Abbrechen schließen das Keyboard; Eingaben/Keyboard selbst bleiben aktiv.
Filteränderungen während laufender Jobs werden nicht zurückgesetzt, sondern nach
Freigabe gespeichert. Erinnerungen zusätzlich: 2 Stunden, 2 Tage, 1 Woche vorher;
vorhandene abweichende Erinnerungswerte bleiben erhalten.

Endstand: acht gezielte Kalender-Tests und vier Architekturprüfungen bestanden.
Gesamtlauf: 268 von 269 Tests bestanden; vorhandener `test_real_gesture_policy`
im Kontrollzentrum weiterhin fehlerhaft, daher Validator nicht grün.
Legacy-360-MHz- und R3-400-MHz-Build erfolgreich. Keine Hardware-Touchprüfung,
kein Flash, kein Commit/Push. Die obige frühere 253-Test-Verifikation ist historisch.
