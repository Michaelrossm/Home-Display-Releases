# Native Fotos und gemeinsame Druckergrundlagen

## Fotos

Die fest integrierte App `photos` verwendet FileBrowserService, StorageService,
NasService und ImageResourceService. Keine eigene Netzwerkverwaltung, Decoder-
oder permanenten FreeRTOS-Tasks. Scans, Einstellungen und Bildzugriffe laufen
asynchron; LVGL-Objekte werden ausschliesslich im vorhandenen UI-Pfad bedient.

Bibliothek, hierarchische Alben, Favoriten, Quellordner, Einzelbild und Bilderrahmen verwenden
denselben Bildpool. Maximal acht Quellordner, 32 Favoriten und 1024 deduplizierte
Bilder je Scan; rekursiv maximal vier Ebenen und 64 Verzeichnisse. Obergrenzen
werden angezeigt, nicht als unbegrenzte Mediathek verkauft. Quellen: SD, USB,
interne Dateien und zentral eingerichtete/aktivierte SMB2-NAS-Freigaben.
Es gelten die vorhandenen Bilddecoder und deren Groessen-/Speichergrenzen;
animierte Formate werden nicht als Video abgespielt.

`/Bilder` wird beim regulären SD-/USB-Mount in der zentralen Ordnerstruktur
angelegt, sofern das Medium beschreibbar ist. Bestehende Dateien bleiben erhalten.
Die Foto-App sucht standardmaessig asynchron in `sd:/Bilder` und `usb:/Bilder`,
einschliesslich Unterordnern; fehlende Medien sind kein Scanfehler. Die automatische
Quelle ist abschaltbar, zusaetzliche ausgewaehlte NAS-/andere Ordner bleiben erhalten.
Alle Fotos werden gemeinsam angezeigt. Die Albumansicht bildet Ordnerhierarchien
ohne Medienpraefix ab: `sd:/Bilder/Urlaub` und `usb:/Bilder/Urlaub` erscheinen als
ein Album `Bilder/Urlaub`. Quelldateien werden weder verschoben noch kopiert;
gleichnamige Dateien auf verschiedenen Medien bleiben eigenstaendige Fotos.
Nach Speicher-/Mount-Aenderungen werden offene Bibliotheks-/Albumansichten
hoechstens alle zehn Sekunden neu eingelesen; manuelles Aktualisieren ist moeglich.
NAS-Aenderungen ausserhalb des Geraets erfordern ggf. manuelles Aktualisieren.

Ein Fisher-Yates-Zufallsbeutel zeigt jedes Bild einmal pro Durchlauf. Zwischen
Durchlaeufen wird eine unmittelbare Wiederholung vermieden (ausser bei nur einem
Bild). Der Zufall stammt aus esp_random; auch ein defekter Zufallslieferant kann
keine Endlosschleife erzeugen.

Der Zeitplan hat Wochentage, Beginn, Ende, Wechselintervall und Rueckkehrzeit.
Neue Standardzeiten sind 07:00 bis 19:00; vorhandene gespeicherte Zeiten bleiben
erhalten. Ueber Mitternacht zaehlt der Starttag; gleiche Zeiten bedeuten ganztags.
Mit aktivierter Uebernahme startet der Rahmen beim Zeitfensterbeginn auch aus
einer anderen App. Danach bleiben andere Apps voruebergehend nutzbar; bei
Inaktivitaet geht es zurueck zum Rahmen, spaetestens vor dem normalen Dimmen.
Ohne Uebernahme bleibt der Start auf Home beschraenkt. Eine Pause gilt fuer das
aktuelle Zeitfenster (bei ganztags bis zum naechsten Tag) oder bis zum Neustart.
Dauerhaftes Abschalten erfolgt ueber die gespeicherte Zeitplan-Checkbox.
Manuell gestartete Wiedergabe ist auch ausserhalb des Zeitfensters moeglich.
Ohne gueltige Uhrzeit kein automatischer Start. Die maximal fuenf Sekunden
gueltige, erneuerte Idle-Lease unterdrueckt Dimmung/Ausschalten waehrend der
aktiven Automatik. Sperrbildschirm/PIN und Sprechanlagenrufe haben Vorrang;
fehlende Bilder beenden die Wiedergabe mit begrenztem Wiederanlauf. Die normale
App-Shell bleibt erhalten.

Sortierung am Display: Name oder Datei-Aenderungsdatum, auf-/absteigend mit
farbig markierter Auswahl. Standard: neueste zuerst, fehlende Datumswerte
zuletzt, stabile Namens-/URI-Aufloesung bei Gleichstand. Das ist kein EXIF-Datum.
Die Sortierung laeuft im bestehenden Worker ueber einen kleinen Indexvektor;
die Zufallswiedergabe bleibt unabhaengig davon.

Fotos besitzt eine native Weboberflaeche unter `/photos` (auch `/apps/photos/`).
Sie erscheint ueber den normalen AppRegistry-Weblink auf der Uebersicht. Alle
Einstellungen/Favoriten verwenden dieselbe `photos_cfg`-Persistenz wie das
Display. Einstellungen laufen als owner-scoped Jobs auf bestehenden Workern;
Revisionen verhindern veraltete Web-/Display-Schreibvorgaenge. Manuelle
Steuerung wird nur atomar zum vorhandenen UI-Task uebergeben.

Uploads verwenden `/photos/upload` und denselben zentralen Datei-Uploadpfad
wie `/files/upload`: Admin-Anmeldung, System-PIN, CSRF, temporaere Datei und
kein Ueberschreiben vorhandener Ziele. Die Fotos-Route erlaubt nur SD/USB unter
`/Bilder/`, unterstuetzte Bild-Endungen und maximal 4 MiB. NAS ist hier nur
Lesequelle. Neue Albumordner verwenden die vorhandene geschuetzte mkdir-Route.
Versteckte Web-Tabs betreiben keine periodischen Abfragen; offene Einstellungen
werden nicht ungefragt ueberschrieben. Nach Aenderungen anderer Clients ist
gegebenenfalls explizites Neuladen noetig.

## Drucker

PrinterActionCatalog stellt gemeinsame Aktionsbeschreibungen und begruendete
Zustaende bereit. Adapter bleiben fuer echte Protokollbefehle und strengere,
geraetespezifische Grenzen verantwortlich. Kataloggrenzen sind keine Freigabe
zum Umgehen der bestehenden Sicherheitspruefungen.

Neue lesende Lua-APIs: `hp_printer_action_catalog(offset,count)`,
`hp_printer_temperature_history(profile,sensor,offset,count)` und
`hp_printer_operation(profile)`. Komponenten verknuepfen stabile Heater-/Tool-/
Extruder-IDs. Bestehende APIs bleiben kompatibel.

Temperaturhistorie: maximal 24 Kanaele mit je 60 Punkten, alle fuenf Sekunden,
bedarfsgerecht in PSRAM. Zeitstempel sind monotone Millisekunden, keine UTC-Zeit.
Der bestehende Profil-/Owner-Cache und dessen Zwei-Minuten-Aufraeumfrist gelten
weiterhin. Keine persistente Langzeitaufzeichnung und keine neue Dauerabfrage
fuer geschlossene Apps.

Befehlszustand unterscheidet queued, accepted, running, completed, failed und
unknown. HTTP-Erfolg allein ist kein Ausfuehrungsnachweis. Insbesondere werden
Kalibrierungen ohne verlaessliche Abschlussinformation nicht als completed
ausgegeben. Keine automatische Wiederholung schreibender Druckerbefehle.
