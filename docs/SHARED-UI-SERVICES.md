# Gemeinsame UI- und Verwaltungsbausteine

## Desktop und App-Einstellungen

Der Desktop zeigt pro Seite einen Punkt oberhalb der Schnellleiste. Die aktive
Seite verwendet die Akzentfarbe; blaettern erfolgt ueber die seitlichen Tasten
oder eine horizontale Wischgeste. LVGL-Callbacks merken nur den Seitenwunsch;
erst die vorhandene UI-Schleife baut die Seite unter DisplayUiGuard um.
Im Bearbeitungsmodus bleiben Wischgesten gesperrt; die Seitentasten funktionieren
ohne laufenden Drag. Eine App am linken/rechten Seitenrand ablegen verschiebt
sie auf die benachbarte vorhandene Seite. Erst nach Finger-up wird persistiert
und umgeblaettert, damit keine aktiven Drag-Objekte geloescht werden. Ein Wischen
darf anschliessend keine App versehentlich oeffnen.

Die gemeinsame Rueckkehr aus App-Einstellungen heisst `Einstellungen` und liegt
in der festen Systemleiste, nicht ueber den App-Inhalten. Der vorhandene Queue-
Pfad oeffnet weiterhin die zentrale App-Verwaltung; App-eigene Rueckkehr zur
Haupt-App bleibt davon getrennt.

`UiTheme::styleInput` ist der gemeinsame Stil fuer native SettingsWidgets sowie
imperative und deklarative Lua-Eingaben und deklarative Auswahllisten. Neue
native Felder verwenden diesen Stil statt eigener fester Dunkelfarben.

## Feldbezogene Tastaturen

`InputKeyboard::bind(keyboard, field)` verbindet bestehende LVGL-Tastaturen mit
dem Eingabetyp des Feldes. Native Apps deklarieren ihn mit
`InputKeyboard::setMode(field, keyboardInput::Mode::Integer)` (alternativ
`Decimal`, `SignedDecimal`, `Phone`, `DateTime`, `Text`). Bereits vorhandene
numerische `accepted_chars` werden ebenfalls erkannt. Passwoerter, Hostnamen,
Postleitzahlen mit moeglichen Buchstaben und Freitext bleiben Texteingaben;
der momentane Feldinhalt wird niemals zur Typbestimmung benutzt.

Lua: `hp_textarea(parent, text, password, maxLength, inputType)` und
`hp_ui_textarea(...)` akzeptieren einen optionalen fuenften Parameter:
`text` (Standard), `integer`, `decimal`/`number`, `signed_decimal`, `phone`,
`datetime`. Nachtraeglich: `hp_set_input_type(field, type)` oder
`hp_ui_input_type(field, type)`, danach Tastatur erneut binden.
Beispiel: `hp_textarea(parent, "200", false, 6, "decimal")`.
Deklarativ: `{id="temperatur", type="input", input_type="decimal"}`.
Alle deklarativen Eingabefelder oeffnen beim Antippen automatisch dieselbe,
erst bei Bedarf erzeugte Tastatur. Ein eigener `on_click` bleibt zusaetzlich
erhalten; die Tastatur wird nicht pro Feld neu erzeugt.
`scrollable=true` macht Seiten/Spalten vertikal scrollbar. Im deklarativen
Root bleibt die Tastatur mit einer gemeinsamen Eingabezeile schwebend am unteren
Rand, damit auch feste Formulare sie nicht aus dem sichtbaren Bereich schieben.
Die Eingabezeile spiegelt das Originalfeld einschliesslich Typ, Laengenlimit,
Mehrzeiligkeit und Aenderungs-Callbacks. Ein Passwort bleibt zunaechst maskiert;
die Augen-Taste zeigt nur die temporaere Eingabezeile an. Schliessen leert diese
und stellt die Maskierung wieder her. Vor dem Entfernen des Originalfelds wird
die Tastatur abgekoppelt; native Apps und Lua verwenden denselben Baustein.
Dezimalwerte verwenden den Punkt, passend zu Lua `tonumber` und den APIs.

Die Zahlentastatur hat grosse, Theme-farbene Tasten, Rueckschritt,
Cursorsteuerung, Schliessen und Fertig. Datum/Telefon haben eigene Zeichen.
Tippen ausserhalb schliesst sie zentral ueber das vorhandene LVGL-Touchgeraet;
Tastatur und andere Eingabefelder sind ausgenommen. Die bestehenden nativen
Abbruch-Handler stellen dabei ihren Ansichtsbereich wieder her.
Maps gelten ausschliesslich fuer das jeweilige Tastaturobjekt; keine globalen
LVGL-Maps, keine neuen Tasks und keine eigenen Tastaturen je App. Die neuen
Lua-Setter unterliegen denselben Owner-/Lifecycle-Pruefungen wie andere UI-APIs.

## Theme-Auswahl

`ThemeCatalog` bleibt die einzige Quelle fuer integrierte und SD-Themes.
`ThemeControls::bindSelection()` bindet sowohl Kontrollzentrum als auch native
Einstellungen an dieselben Optionen und denselben Aenderungs-Callback.
Die Webverwaltung laedt Namen/IDs aus `/api/settings`; Namen werden als Text,
nicht als HTML eingefuegt. Katalogzugriff und Web-Theme-Aenderungen sind durch
`DisplayUiGuard` mit der bestehenden LVGL-Verwaltung synchronisiert.

## Web-Anmeldung

`WebAdminAuth` verwendet das bestehende zentrale Secret `web_admin_pwd`.
Bestehende Kennwoerter bleiben erhalten; nur fuer ein neues, beschreibbares
Geraet wird der bisherige Standard `Passwort` gespeichert. Schreibfehler und
unlesbare vorhandene Secrets erlauben keine Anmeldung mit einem Ersatzwert.
Es gibt keinen dauerhaften Klartext-Kennwortcache. Aenderungen gelten damit
auch fuer den Verwaltungsdienst, ohne dessen Neustart.

Port 80, Verwaltungsdienst und Recovery behalten ihre eigenen Routen,
Antwortheader, Challenges und vorhandenen Schreibschutz-/CSRF-Regeln.
Recovery haengt nicht vom normalen Webserver oder UI ab. Basic Auth ueber HTTP
ist weiterhin unverschluesselt: nur im vertrauenswuerdigen LAN verwenden.

## Kalender-Auftraege

`executeCalendarJob()` uebernimmt Abbruchpruefung, begrenztes JSON-Decoding,
Aufruf der bestehenden Kalender-API und begrenzte Ergebnisserialisierung.
Native UI, Lua und Web verwenden weiterhin denselben bestehenden Jobdienst;
Besitzer, Berechtigungen und frontend-spezifische Schutzpruefungen bleiben
erhalten. Abbrueche waehrend einer synchronen Ausfuehrung verwerfen das
Ergebnis; sie behaupten nicht, eine bereits begonnene Schreiboperation
rueckgaengig zu machen. Native Ansichtseinstellungen bleiben lokal im
Controller. Keine neuen Tasks, Timer oder Kalenderdatenbanken.

## Gemeinsame Postleitzahlsuche

Standardland: `RegionService` liest `region_country` einmal beim Boot und
stellt einen gemeinsamen Laenderkatalog fuer native UI, Lua und Web bereit.
Deutschland ist der Standard. Einstellungen unter System → Region (Display
und Web); Aenderungen werden ohne UI-Lock ueber den bestehenden lokalen
Transferworker gespeichert. Fehlgeschlagene Speicherung aendert den Cache nicht.
Leeres Land bedeutet Firmware-Standard, explizites Land bleibt unveraendert.
`CountryPicker` erhaelt vorhandene Adressdaten ueber ein verborgenes Modellfeld;
die sichtbare Bedienung ist ausschliesslich eine Liste. Unbekannte Altwerte
bleiben unveraendert erhalten. Read-only-Modelle lassen keine Aenderung zu.
Lua: `hp_region()` → `{version=1,country="DE",countries={{code="DE",name="Deutschland"},...}}`.
Web: `/api/region`; `hpWeb.countrySelect()` nutzt denselben Katalog.
Keine automatische Geolokalisierung oder Aenderung der Feiertagsregion.
Die deklarative UI bietet `type="dropdown"`, `options` (Zeilen mit LF),
`value` (nullbasierter Index) und `on_change`; Lesen mit `ui.read(id)`.
`hp_ui_symbol(name)` liefert Symbole des bestehenden IconCatalog.

`PostalLookupService` ist der einzige PLZ-Provider fuer Kalender, Telefonbuch
(Display/Web) und Lua-Apps. Er prueft Land und PLZ, laesst keine freien URLs zu
und liefert hoechstens 15 Orte mit Stadt, Region, Land und optionalen Koordinaten.
HTTPS mit Zertifikatspruefung, maximal 8 KiB Antwort und 5-Sekunden-Frist.
Bereits blockierende Transportaufrufe sind nicht sofort unterbrechbar;
Abbruch und Fristueberschreitung verwerfen deren Ergebnis.
Weitere Laender verwenden ISO-Zweibuchstabencodes; Datenabdeckung ist begrenzt.
`PostalLookupJob` passt den Dienst an vorhandene owner-scoped Jobs an.
Lua: `hp_postal_lookup({postal_code="93495",country="DE"})` benoetigt
`network.internet` und liefert einen normalen Job-Handle. Ergebnis ueber
`hp_job_result`, JSON in `body`, Abschluss `http.completed`; abbrechen/freigeben
wie andere Jobs. Keine neuen Tasks. Fehler lassen manuelle Eingaben zu.
Die Kalender-API bleibt abwaertskompatibel; neue Koordinaten sind Zusatzfelder.

## Lua-Abmessungen

`hp_set_size` erhaelt die LVGL-Sonderwerte fuer
`LV_SIZE_CONTENT` und 0 bis 100 Prozent unveraendert. Nur normale Pixelwerte
werden auf 1 bis 4096 begrenzt. Die deklarative UI unterstuetzt
`width_percent` und `height_percent` (0 bis 100); Aenderungen einer Achse
erhalten die Einstellung der anderen Achse. Keine eigene Layout-Engine.

## Bewusst erhaltene Grenzen

Das native Kalenderlayout und Lua-Monatswidget haben unterschiedliche
Darstellungsaufgaben, teilen aber bereits das Datumsmodell. Externe Dateien
und geschuetzte interne Systemdateien behalten getrennte Dienste. Die alten,
ausgeschlossenen nativen App-Oberflaechen werden weiterhin nicht kompiliert;
sie erzeugen keine laufenden Timer. App-Laufzeit, Paketverwaltung und gemeinsame
App-Uebersicht werden nicht zu einer zweiten Architektur umgebaut.
