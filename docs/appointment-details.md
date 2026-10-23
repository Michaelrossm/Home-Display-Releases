# Terminangaben und optionales lokales Telefonbuch

`EventRecord` bleibt binär unverändert. `EventDetails` enthält vollständigen Ort,
Straße, Hausnummer, PLZ, Stadt, Region, Land, Telefonnummer und Notizen (bis 4096
UTF-8-Bytes). Größere Providerangaben werden UTF-8-sicher begrenzt und durch
`truncated` gekennzeichnet. Eine Hausnummer wird nicht aus Freitext geraten.

Google liefert Ort und Beschreibung; Microsoft zusätzlich die postalische
Adresse. Microsoft benötigt für Notizen `Calendars.Read`, nicht
`Calendars.ReadBasic`: bestehende Konten gegebenenfalls erneut freigeben.
Provider-HTML bleibt gekennzeichnet; Apps dürfen es nicht als aktives HTML
rendern. Telefonnummern werden nur gespeichert, wenn tatsächlich vorhanden;
aus Beschreibungen werden keine Nummern geraten.

`CloudCalendarClient::copyEventDetails` liest ausschließlich den Speicher-Cache.
Offline-Snapshots sind mit der zugehörigen Terminliste durch Prüfsummen verbunden.
`CalendarService::saveEventDetails/loadEventDetails` speichern lokale Details über
StorageService (nur im Worker benutzen). Der CalDAV-Serializer hat einen
Details-Overload für Ort, Notizen und Telefonnummer. Dies ist kein Cloud-PUT:
die aktuellen Google-/Microsoft-Connectoren bleiben lesend.

`PhonebookService` verwaltet bis zu 128 lokale Kontakte pro Konto mit E-Mail,
Telefon, Mobilnummer, Adresse und Notizen. Auch diese Speicher-API ist worker-only.
Die Daten sind an Konto-ID **und** E-Mail gebunden. Ein neu eingerichtetes Konto
bekommt keinen Zugriff auf das Telefonbuch einer vorherigen E-Mail.
Der Ein/Aus-Schalter in der nativen Kontoverwaltung wird zentral gespeichert
und ist standardmäßig aus. OAuth-Kontaktrechte benötigen den separaten Schritt
„Kontakte verbinden / freigeben“.

In der Kontoverwaltung öffnet „Telefonbuch & Termindetails anzeigen“ eine
durchsuchbare native Ansicht. Lokale Kontakte lassen sich anlegen, bearbeiten
und nach zusätzlicher Bestätigung löschen; Anbieter-Kontakte und Termine bleiben
lesend. Notizen aus Provider-HTML werden als Klartext angezeigt.

Kontaktimport: Google People API, Microsoft Graph `/me/contacts` sowie CardDAV
(iCloud oder eigener Server) über den vorhandenen NetworkWorker. Google/Microsoft
benötigen eine ausdrückliche zusätzliche Lesefreigabe. Kontakte-Rechte werden nie
beim normalen Profil-/Kalenderlogin automatisch angefordert. Bei bereits
freigegebenem Kalender wird dessen Leserecht beim Kontakte-Login erhalten.

Lokale Kontakte und Anbieter-Snapshots liegen in getrennten Dateien; wiederholter
Abgleich ersetzt nur den Anbieter-Snapshot. Bis zu 128 lokale und 128 importierte
Kontakte je Konto; größere Bestände und begrenzte Felder werden kenntlich gemacht.
Provideränderungen werden alle fünf Minuten gelesen, solange das Telefonbuch
eingeschaltet ist. Ausschalten verhindert weitere Kontaktabrufe, ohne Cloud-Daten
zu löschen. Entfernen des Kontos löscht beide lokalen Kontaktdateien.

CardDAV: HTTPS/Basic mit gültigen Zertifikaten; vCard 3.0/4.0 in UTF-8. vCard 2.1
und alte Transfercodierungen werden mit sichtbarem Fehler abgelehnt, nicht als
kaputte Zeichen gespeichert. Eigene Server werden über `/.well-known/carddav`
am konfigurierten HTTPS-Ursprung bzw. den konfigurierten Pfad gefunden.
Kein Cloud-Schreiben von Kontakten oder Terminen. Live-Anmeldungen/Sync sind ohne
bereitgestellte Anbieterregistrierungen und Benutzerfreigaben nicht verifiziert.
