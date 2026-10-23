# Gemeinsame Benutzerverwaltung

## Einstieg

- Browser: `/users` (auch `/users/`), direkt aus Übersicht, Kalender und Telefonbuch.
- Display: **Systemeinstellungen → Benutzer**; Kalender und Telefonbuch verlinken nur dorthin.
- Browser: **Systemeinstellungen → Benutzer** öffnet dieselbe zentrale `/users`-Ansicht.
- Keine Benutzerformulare mehr innerhalb der allgemeinen Kalendereinstellungen.

Die Verwaltung besitzt eine durchsuchbare Benutzerliste und einen separaten
Profilbereich. Die Browseransicht ordnet diese auf schmalen Geräten untereinander
an. Profile zeigen getrennten Vor-/Nachnamen, optional eine Kontakt-E-Mail,
Geburtstag, Farbe, Telefonbuch-Verknüpfung, bis zu vier normale Kalender und die
vorhandenen Kalenderkonten. Feiertage/Geburtstage bleiben gemeinsame Funktionen.
Kontaktlisten können aus einem Profil mit dem Benutzerfilter geöffnet werden.

## Eine Datenbasis, kein zweites Benutzersystem

Die vorhandenen CalendarService-Personen bleiben die gemeinsamen Identitäten
des Telefonbuchs und Kalenders. Die native Ansicht nutzt die bestehende
Kalender-Profilkomponente innerhalb der Systemeinstellungen. Alte Navigation mit
`calendar {"view":"users"}` wird auf die Systemeinstellungen umgeleitet;
Navigation bleibt über UiShell queued. Die Webansicht verwendet dieselben
Kalenderoperationen und vorhandenen Jobs. Es gibt weder eine zweite Datenbank
noch neue permanente Tasks. Alte Phonebook-`user_save`-Aufrufe bleiben kompatibel;
die bisherigen getrennten Telefonbuch-Benutzereditoren wurden entfernt.

Die neue optionale API-Eigenschaft `contact_email` ist eine bearbeitbare
Profilangabe, **keine interne Identität oder Zugangsinformation**. Sie liegt
ID-bezogen im vorhandenen `cal_person_ui`-Metadatensatz unter `emails`.
`people` liefert sie zusätzlich zu unverändertem `email`/`id`. Alte Profile
werden ohne Migration gelesen. Neue Benutzer der gemeinsamen Oberfläche erhalten
eine freie interne Identität; gleiche Kontakt-E-Mails dürfen bei unterschiedlichen
Profilen vorkommen. Ändern der Kontakt-E-Mail verändert keine vorhandene ID,
Kalender-/Kontenzuordnung oder Telefonbuchreferenz. Der gemeinsame Metadatensatz
bleibt auf 6 KiB beschränkt; höchstens 16 Profile, Kontakte und Ansichten werden
validiert. Die binären Personen-/Kalenderdaten und das Konfigurationsschema bleiben
unverändert.

## Bearbeitung und Schutz

Hinzufügen/Bearbeiten wird ausdrücklich gespeichert. Profilwechsel warnen bei
ungespeicherten Änderungen; der Browser schützt auch Verlassen der Seite und
erhält Entwürfe bei Sichtbarkeitswechseln. Während nativer Speicherung sind
Eingaben gesperrt. Kontenzuordnung verwendet die vorhandene zentrale Verknüpfung;
Passwörter/OAuth bleiben in der Kontenverwaltung.

Benutzer sind **Ansichtsprofile, keine zusätzlichen Administrator-Anmeldungen**.
HTTPS-Adminanmeldung, System-PIN und CSRF bleiben unverändert erforderlich.
Keine vorhandenen Benutzer, Termine oder Kontakte werden automatisch gelöscht.

Die reine native Benutzeransicht fragt keinen Monats-Terminbereich ab und lädt
keine Feiertagsregion. Profiljobs verwenden den vorhandenen lokalen Workerpfad,
damit reine Metadatenbearbeitung keine WLAN-Aktivität auslöst. Nur die offene
Ansicht besitzt ihren bisherigen LVGL-Timer; Schließen räumt die Ansicht/Jobs auf.

Lokale native Aufträge bleiben Job-Typ 5 und nutzen `Lane::LocalTransfer`.
Typ 1 ist ausschließlich die bestehende sandboxed Storage-API und darf keine
Kalender-/Benutzer-Handler tragen. Kalenderberechtigungen reichen aus; zusätzliche
Storage-Berechtigungen sind nicht erforderlich. Reine Leseaufträge halten die
globale PIN-Schreibsperre nicht über Dateizugriffe. Sperrreihenfolge bei Änderungen:
Profilkoordination, PIN-Schreibschutz, Kalender-Metadaten.

## Zentraler Profilvertrag

Neue Profile erhalten zufällige dauerhafte IDs; Namen und Kontakt-E-Mail sind
änderbar und niemals neue Identitäten. Bestehende Kalender-Personen- und Kontakt-IDs
sowie binäre Datenformate bleiben erhalten. Alte separate Telefonbuch-Benutzer
werden eindeutig über ihre vollständige alte ID auf zentrale Profile abgebildet;
Kontaktzuordnungen werden gemeinsam migriert, nicht anhand gleicher Namen geraten.
Der bisherige `users`-Index im Telefonbuchformat bleibt ein abgeleiteter
Kompatibilitätsindex und besitzt keinen eigenen Editor oder Schreibpfad.
Inaktive Datensatzplätze werden nicht zusammengezogen, damit
Teilnehmer-Bitmasken bestehender Termine stabil bleiben. Maximal 16 aktive Profile.

Profil, optionaler persönlicher Kalender (standardmäßig an) und verknüpfter
Telefonbuchkontakt werden über `CalendarService::saveProfile()` gespeichert.
Telefon, Mobilnummer, E-Mail und vollständige Adresse liegen ausschließlich im
verknüpften Kontakt, nicht in einer zweiten Profil-Datenbank. Weitere Nummern,
E-Mails und Adressen bleiben bei Änderungen erhalten. Ein vorhandener lokaler
Kontakt kann ausdrücklich ausgewählt werden; fremde Profilkontakte werden nicht
übernommen. Kontaktbearbeitung aktualisiert das zugehörige Profil ebenfalls.

`/calendar/profile.commit.bin` ist nur ein begrenzter, prüfsummengeschützter
Wiederherstellungsauftrag für die bestehenden Personen-, Kalender-, Kontakt- und
Optionsdateien. Er wird vor den Einzeländerungen atomar geschrieben, bei Neustart
idempotent fertiggestellt und danach entfernt. Bei einem unvollständigen Auftrag
wird die Bearbeitung gesperrt, statt einen teilweise gespeicherten Benutzer
anzuzeigen. Beschädigte Aufträge werden nicht stillschweigend gelöscht.

Neue Anlage-Aufträge unterstützen `request_id` (16 Hexzeichen) für sichere
Wiederholung nach verlorenen Antworten. Bestehende Profile unterstützen `revision`
gegen das Überschreiben gleichzeitiger Änderungen. Löschen braucht Bestätigung;
Termine, Konten, fremde Kalender und weitere Ansichtsverknüpfungen verhindern es.
Kontakte bleiben erhalten, leere persönliche lokale Kalender dürfen entfernt werden.

Native und Web-Ansichten haben getrennte Bereiche **Profil**, **Kontakt** und
**Verknüpfungen**. Sichtbare Listen reagieren auf Revisionen; laufende Entwürfe
werden nicht durch Hintergrundaktualisierung überschrieben. Unsichtbare
Browserseiten fragen keine Änderungen ab. Es gibt keine zusätzlichen Tasks.

Lua nutzt dieselben Berechtigungen und vorhandenen asynchronen Jobs:
`hp_users_list`, `hp_user_profile`, `hp_user_save`, `hp_user_remove`,
`hp_users_revision`; die bestehenden `hp_calendar_*`-Aufrufe bleiben kompatibel.
`hp_open_user_settings()` stellt ausschließlich einen UI-Navigationsauftrag.
Profil-ID und Kontakt-ID werden getrennt geliefert. `participant_mask` in `people`
ist die stabile Teilnehmermaske; neue Verbraucher dürfen sie nicht aus der
Position in einer gefilterten Liste berechnen. Feiertage und Geburtstage zählen
weiterhin nicht zu den maximal vier normalen Kalendern pro Ansicht.

## Verifikation

Hosttests prüfen unter anderem Suche/Sortierung, Auswahlschutz, gemeinsame
Routen, fehlende Doppel-Editoren, Profil-E-Mail-Validierung/Persistierung und
unveränderte Zuordnungen. Browser-/Geräteprüfung ist zusätzlich erforderlich;
ein erfolgreicher Build ist keine Hardwarevalidierung.
