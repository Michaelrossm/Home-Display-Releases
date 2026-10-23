# Kalender: PLZ-Suche und Handy-Einrichtung

## Web-Anmeldung

`https://<Geräte-IP>/login` ist die zentrale Anmeldeseite. Nicht angemeldete
Seitenaufrufe werden mit ihrer lokalen Zieladresse dorthin weitergeleitet.
API-Aufrufe liefern stattdessen 401. HTTP bleibt reine HTTPS-Weiterleitung.
Die vorhandenen Admin-Zugangsdaten gelten weiterhin. Recovery und nicht-
Browser-API-Clients behalten ihre bestehende Basic-Authentifizierung.

Normale Sitzungen gelten maximal zwölf Stunden und enden beim Geräteneustart.
„Angemeldet bleiben“ gilt höchstens 30 Tage, setzt eine gültige Gerätezeit
voraus und überlebt Neustarts. Maximal acht Sitzungen; keine permanente Task.
Cookies: zufällige 256-Bit-Tokens, `Secure`, `HttpOnly`, `SameSite=Strict`,
`__Host-`-Präfix. Nur SHA-256-Hashes werden im vorhandenen verschlüsselten
Credential-Speicher abgelegt. Abmeldung widerruft die aktuelle Sitzung.
Eine Admin-Passwortänderung erhöht zuerst die persistente `web_auth_rev` und
widerruft alle bisherigen Sitzungen, auch bei Wiederherstellung des alten
Passworts. Speicherfehler dürfen keine erfolgreiche Persistierung vortäuschen.
PIN und bestehender CSRF-Schutz bleiben unabhängig erforderlich.

## iCloud per Handy

Der gemeinsame QR-Generator erzeugt für Apple jetzt die lokale Adresse
`https://<Geräte-IP>/calendar?connect=icloud`. Nach der Home-Display-Anmeldung
öffnet diese die iCloud-Einrichtung. Der QR enthält keine Zugangsdaten.
Der Browser muss das vorhandene Geräte-Zertifikat akzeptieren bzw. diesem
vertrauen; IP-Wechsel können einen neuen QR erforderlich machen.

Das ist **keine Apple-OAuth-Kalenderfreigabe**: für den vorhandenen CalDAV-
Connector ist weiterhin ein bei Apple erzeugtes App-Passwort erforderlich.
Dieses kann auf dem Handy geschützt über HTTPS eingegeben werden. Kontoplatz
und Freigaben werden ausdrücklich gewählt; keine unbekannten Konten überschreiben.
Google/Microsoft verwenden ihre vorhandenen offiziellen Anmeldeflüsse.

## PLZ-Suche

Browser und Display ergänzen Ortsdaten nach Verlassen des PLZ-Felds.
Deutschland ist bei leerem Land Standard; unterstützt werden DE (fünf Ziffern),
AT und CH (vier Ziffern). Vollständiger Ländername oder ISO-Code ist erlaubt.
Mehrere Treffer brauchen eine Ortsauswahl. Vorhandene manuelle Angaben bleiben
erhalten. Veraltete Antworten dürfen geschlossene oder geänderte Formulare
nicht überschreiben. „Ort“ als Veranstaltungsort bleibt ebenfalls unverändert;
die postalische Stadt steht im separaten Stadt-Feld.

Über den vorhandenen NetworkWorker werden **nur Land und PLZ** an
`https://api.zippopotam.us/` gesendet. Zertifikatprüfung, keine Weiterleitungen,
4-Sekunden-Netzwerk-Timeout, zusätzlich fünf Sekunden Prüfung bei Rückmeldung,
8 KiB Antwortgrenze und höchstens 15 Orte. Laufende synchrone Netzwerkaufrufe
sind nicht beliebig sofort unterbrechbar; Abbruch wird vor/nach und während
gelieferter Daten geprüft. Offline, Zeitfehler, fehlende oder uneindeutige Daten
lassen weiterhin die manuelle Adresseingabe zu. Keine lokale vollständige PLZ-Datenbank.

„Gesamtkalender“ ist ausschließlich eine Ansicht aller Personen. Es wird kein
Benutzer „Alle Benutzer“ angelegt oder als Terminperson gespeichert.
