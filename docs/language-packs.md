# Zentrale Sprachpakete

Derzeit wird ausschliesslich **Deutsch (de-DE)** ausgeliefert. Die Auswahl liegt
am Display unter Einstellungen -> System -> Sprache sowie im Browser unter
Einstellungen -> System. Es werden keine unvollstaendigen Platzhalter-Sprachen
angeboten. NVS-Schluessel: `ui_lang` (unter 15 Zeichen); UI-Reset entfernt ihn.

## Speicher und Aktivierung

`resources/languages/de-DE.json` ist die gepflegte UTF-8-Quelle. Das Paket
`de-DE.zip` enthaelt genau eine Deflate-komprimierte `locale.json` mit Schema,
Locale, Anzeigename, Schreibrichtung und Textschluesseln. Der Generator erzeugt
zusaetzlich `src/services/languages/LanguageAssets.h`. **Nur ZIP-Bytes**, nicht die
JSON-Quelldateien, werden als Sprachdaten in die Firmware eingebunden.

Aufruf: `python tools/package_languages.py`; Konsistenzpruefung: gleicher Befehl
mit `--check`. Es werden reproduzierbare ZIP-Dateien erzeugt. Zur Einfuehrung
enthaelt das deutsche Paket 66 zentrale Texte. Die Sprachdatei ersetzt noch
nicht saemtliche vorhandenen deutschen C++-/HTML-Literale; es wird deshalb keine
pauschale Verkleinerung der gesamten Firmware behauptet.

Das benoetigte Paket wird vor dem UI-Start einmal entpackt. Nachschlagen enthaelt
keine Dateizugriffe, Dekompression oder dynamische Allokation. Daten und sortierte
Suchindizes liegen in PSRAM und bleiben fuer den Bootzyklus unveraenderlich.
Bei einer spaeteren Fremdsprache wird zusaetzlich Deutsch als Rueckfall geladen;
bei Deutsch nur ein einziges Paket. Eine Auswahl wird im vorhandenen lokalen
Transferworker validiert und gespeichert; wirksam wird sie nach Neustart.
Keine neue permanente Task und kein UI-Neuaufbau aus dem Worker.

### SD und USB

Die erzeugten ZIP-Dateien koennen unveraendert unter `/Sprachen/<locale>.zip`
auf SD oder USB liegen, aktuell also `/Sprachen/de-DE.zip`. Der gemeinsame
Speicheraufbau legt `/Sprachen` auf beschreibbaren externen Medien an, ohne
vorhandene Dateien zu ersetzen. Pakete muessen zu einer registrierten Sprache
passen (Locale und Anzeigename); unbekannte ZIP-Dateien werden nicht automatisch
als neue Sprache angeboten. Neue Sprachen werden zuerst im zentralen Katalog
registriert, wie unten beschrieben.

Beim Booten wird nach dem Start der Speicherdienste fuer die ausgewaehlte Sprache
zuerst SD, dann USB geprueft. Nur ein vollstaendiges, gueltiges Paket ersetzt das
eingebettete Paket. Defekte, zu grosse, unvollstaendige oder fremde Pakete werden
ignoriert. Die ZIP-Datei wird nicht auf den Datentraeger entpackt. Danach sind
keine Medienzugriffe fuer Textanzeigen erforderlich; Abziehen des Mediums laesst
die geladenen Texte unveraendert. Deutsch bleibt fest als Rueckfall eingebaut.

Einlegen/Austauschen wird beim naechsten Neustart beruecksichtigt. Ein USB-Medium,
das erst nach der Boot-Pruefung bereit wird, wird erst beim folgenden Start
verwendet; der Bootvorgang wartet nicht unbegrenzt auf USB. Es gibt keinen
permanenten Suchlauf, zweiten USB-Treiber oder Dateizugriff unter dem UI-Lock.

Grenzen: 256 KiB ZIP, 512 KiB entpacktes JSON, 4096 Texte, 96 Byte pro Schluessel,
2048 Byte pro Text, 1 MiB Parser-Allokationsbudget. Ein Paket darf nur eine
Datei enthalten: keine Pfade, Verschluesselung, ZIP64, Daten-Deskriptoren oder
Anhaenge. Header, Laengen, Stream-Ende, verbrauchte Bytes und CRC werden geprueft.
Bei fehlender/defekter Sprachwahl bleibt Deutsch aktiv; kein Formatieren und kein
automatisches Ueberschreiben der gespeicherten Auswahl.

## Gemeinsame APIs

- C++: `hp::tr("common.save", "Speichern")` liefert einen fuer den Bootzyklus
  gueltigen Zeiger. Unbekannte Schluessel fallen auf Deutsch bzw. den expliziten
  Rueckfalltext zurueck. Rueckgabewerte sind niemals printf-Formatvorlagen.
- Bestehende gemeinsame `SettingsWidgets::label`-Aufrufe nutzen eine deutsche
  Textzuordnung als Uebergang. Neue Komponenten sollen stabile Schluessel nutzen;
  unterschiedliche Kontexte mit gleichem deutschen Wort brauchen eigene IDs.
- Lua: `hp_language()` und `hp_tr("common.save", "Speichern")`; beide sind reine,
  begrenzte Lesezugriffe und auch im Background-Callback erlaubt. Kein fremder
  App-State und keine Sprachumschaltung aus Lua-Callbacks.
- Web: authentifiziertes `GET /api/language` liefert den Katalog; optional
  `?keys=common.save,common.cancel` liefert bis zu 16 Texte, maximal 32 KiB.
  `POST /api/language?id=de-DE` verlangt Admin-Anmeldung, CSRF und freigegebene
  Einstellungen. Der UI-Guard schuetzt nur die kurze Uebergabe, niemals NVS-I/O.

## Weitere Sprache spaeter hinzufuegen

1. Vollstaendige `<locale>.json` nach dem deutschen Schema anlegen. Der Generator
   verlangt gleiche Schluessel und Platzhalter; doppelte JSON-Schluessel, falsche
   Typen, Steuerzeichen und ungueltige IDs werden abgewiesen.
2. Alle noch hart codierten sichtbaren Texte der betroffenen Module auf zentrale
   Schluessel umstellen und den deutschen Katalog entsprechend erweitern. Dazu
   gehoeren dynamisch zusammengesetzte Saetze, HTML, App-Namen und Datumsformate;
   Protokollnamen, KV-Schluessel, Benutzerdaten und G-Code werden nicht uebersetzt.
3. Schriftabdeckung, Datums-/Zahlenformat und Layout mit echten Texten pruefen.
   Derzeit wird nur LTR akzeptiert; RTL braucht zusaetzlich echten Layout- und
   Schrift-Support und darf nicht nur als weiterer Dropdown-Eintrag erscheinen.
4. Generator, Tests, Validator und passenden P4-Build ausfuehren. Erst danach die
   Sprache ausliefern. Es gibt bewusst keinen ungesicherten ZIP-Webinstaller.

Hosttests pruefen echte ZIP-Daten und den Parser. Der Service-Lifecycle-Test
ersetzt die Zlib-Funktionen explizit durch ein Fixture, waehrend der P4-Build
gegen die vorhandene, gepinnte Zlib-Implementierung linkt. Kompilation ist kein
Hardware-/Schrifttest.
