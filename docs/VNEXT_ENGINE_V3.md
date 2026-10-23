# Firmware VNext / Netzwerk-Engine V3

Lokaler Umbau auf Basis von `ac2ec47b175eac8f237d0bc13846793df06a56f6`.
Vorhandene lokale Änderungen hatten Vorrang. Keine Git-Commits, Pushes,
App-Migration, Datenlöschung, Framework-Upgrades oder Hardware-Flashs.

## Abgrenzung der ursprünglichen Fehler

Die Ausgangssuite hatte 10 Fehlschläge und einen Fehler. Der Validator meldete
zusätzlich das vom Benutzer bewusst gewählte Standardpasswort als Geheimnis.

- `test_app_lifecycle`: Der lokale Performance-Patch hatte Abhängigkeiten auf
  `micros`, PowerManager und UiPerformanceMonitor eingeführt; die nativen
  Test-Doubles fehlten. Integrationstest korrigiert, Produktionslogik erhalten.
- Worker-/Performance-Tests: Veraltete Erwartung eines einzigen Workers,
  obwohl bereits ein Haupt- und ein Transferworker vorhanden waren. Fragiler
  Whitespace-Parser erzeugte den Testfehler; Maintenance gehört zum Hauptworker.
- Printer-Komponenten/Startup: Profilrefresh wurde in Tests als impliziter
  Seiteneffekt vorausgesetzt. Produktionscode besitzt einen expliziten
  Refresh und behält bei Leseproblemen den letzten gültigen Profilcache.
- Printer-Dateien: Alte Grenzwerte 60 Sekunden/16 MiB entsprachen nicht den
  vorhandenen 300 Sekunden/64 MiB. Tests an tatsächliche Grenzen angepasst.
- Printer-Service: Vorhandene Profil-TTL beträgt 120 Sekunden, nicht 70;
  Reconnect und Profilladen können zwei Wartungsdurchläufe benötigen.
- UI-Poll-Test: Prädikat passte nicht mehr zur vorhandenen `p.text`-Prüfung.
- Tatsächlicher vorhandener Produktionsfehler: RRF-Metadaten wurden nach der
  ersten erfolgreichen Abfrage dauerhaft als bekannt behandelt. Jetzt 30-s-TTL.
- Validator: Nur die exakte Standardpasswort-Zuweisung in den beiden bestehenden
  Web-/Recovery-Authentifizierungsmodulen ausgenommen. Andere Zuweisungen,
  zusätzliche Geheimnisse oder andere Dateien bleiben Fehler. Gezielt getestet.

Neue Test-Doubles mussten außerdem für Deadline-Setter, Config-Revision,
Lua-Registry und Cancel-Atomics ergänzt werden. Diese zwischenzeitlichen
Testfehler wurden nicht durch Entfernen oder Überspringen von Tests kaschiert.

## App-Engine und Desktop

`LuaApp`, AppManager, UiShell, HomeReorderController und bestehende Bridges
bleiben die einzige App-/UI-Architektur.

- Schema 3 strikt validiert; alte Pakete erhalten
  `manifest_schema_v3_required`. Bestehende Dateien bleiben unangetastet.
- Vorbereitung auf bestehender Priority-Queue, mit eigener langlebiger
  Job-Kopie, Package-IO-Sperre und abbrechbarem 512-Byte-Lesen. Keine Lua-
  oder LVGL-Aufrufe im Worker. Scriptgrenze 192 KiB, Lua-State-Budget 1 MiB.
- Preparing -> Opening -> Foreground; Suspend/Resume mit demselben geparkten
  State und Widgets; Closing/Evicted und Error explizit. Optionale Callbacks:
  `on_open`, `on_suspend`, `on_resume`, `on_close`, `on_destroy`.
- Normale Callbacks 10 ms, Initialisierung/on_open 20 ms, Tick 12 ms;
  Hintergrund maximal 10 ms/20.000 Instruktionen. UI-Bridges im Hintergrund,
  im geparkten oder schließenden State gesperrt. Fehler quarantänisieren nur
  die betreffende App. Keine zweite LVGL-Timer-Task.
- Permanenter Desktop als eigener Host neben dem App-Host. Rebuild nur bei
  Registry-/Theme-/Seitenänderung, sonst Home-Rückkehr ohne Objekt-Neuaufbau.
  Tiles/Reorder über IDs, Revisionsprüfung gegen veraltete LVGL-Objekte.
- Drei suspendierte Lua-States im bestehenden LRU; RAM-Mangel eviktiert sicher.
  Disable räumt im nächsten UI-Guard-Durchlauf auf, nicht im Web-Callback.

## Deklarative API

`ui.version == 3`, private firmwareseitige Pump-Funktion je Lua-State:

```lua
function on_open()
  ui.mount{id='page', type='page', children={
    {id='title', type='label', text='Beispiel'},
    {id='action', type='button', text='Ändern', on_click='change'}
  }}
end
function change() ui.patch('title', {text='Nur dieses Widget geändert'}) end
```

`ui.mount` einmal je State, `ui.patch(id, props)`, `ui.read(id)`, `ui.ready()`,
`ui.percent(n)`, `ui.dialog(title, text)`, `ui.back()` (eigene Haupt-App),
`ui.chart(id, seriesA, seriesB)`. Typen: page/row/column/panel, label/button,
input/switch/progress/loading, list/item, tabs/tab, chart. Vorhandene Widgets,
Theme und Eigentumsprüfungen werden wiederverwendet.

Grenzen: 256 Nodes, Tiefe 16, ID 48 Bytes, Callbackname 96 Bytes,
Text 2048 Bytes, maximal acht Widget-Arbeiten je Pump und kooperatives
2-ms-Fenster, äußerer Lua-Watchdog 5 ms. Charts maximal zwei Reihen/32 Punkte.
Patches werden pro ID zusammengeführt; unveränderte Eigenschaften erzeugen
keine Bridge-Mutation. Dynamische Node-Insertion ist noch keine API:
Listen/Seiten besitzen vorab deklarierte Slots.

## Gemeinsame Daten und Netzwerk

`data.subscribe(id, {source='energy'|'printer'|'calendar', profile=..., query=...,
on_change='callback'})`, `data.snapshot`, `data.refresh`, `data.unsubscribe`.
Maximal acht unterschiedliche Queries und vier Listener je Query/State.
Gleiche Subscriptions teilen vorhandene Cache-/Job-Ergebnisse, ohne neue
Provider, Polling-Timer oder Tasks. Envelope enthält value, eigene revision,
provider_revision, timestamp_ms, loading, offline und error. Providerzeit
wird verwendet, soweit verfügbar, sonst Zeitpunkt des lokalen Snapshot-Lesens.

- Energy-/Printer-Cache sofort lokal; Kalenderquery über owner-scoped lokalen
  Storagejob, niemals Cloud-HTTP unter dem UI-Lock. Ereignisse invalidieren
  nur betroffene Subscriptions; zwei lokale Reads pro Durchlauf.
- `hp_printer_watch(profile, enabled)` hält den sichtbaren abonnierten Drucker
  über den bestehenden Scheduler aktuell, ohne Snapshot-Polling der App.
  Geparkte Apps nutzen weiterhin begrenzte Hintergrundintervalle.
- Energy-Timestamps allein erzeugen keine neue Inhaltsrevision/GUI-Meldung.
  Kalender lokale Änderungen und geänderte Cloud-Caches erzeugen Ereignisse.
- Priority-Queue weckt den vorhandenen NetworkWorker direkt; Fairness drei
  Priority-Jobs, dann ein Normaljob. Hintergrundwartung startet nicht vor
  bereits wartenden Priority-Aktionen. Transfers bleiben getrennt.
- RRF-Leseabläufe geben zwischen synchronen Requests bei neuer Priority-Arbeit
  nach. Kein partieller Status wird veröffentlicht; letzter Cache bleibt gültig.
  Schreibbefehle und bestehende Reservierungen werden nicht verändert/replayed.
- HTTP-Deadlines prüfen Zeit/Abbruch zwischen begrenzten Read-/Write-Schritten;
  nächster nativer Timeout wird an Restzeit angepasst, niemals Timeout 0.
- Optional `hp_http_get{url=..., coalesce=true}` nur für ausdrücklich
  nebenwirkungsfreie GETs. Gleicher Owner, URL, Kontext/Profil, Berechtigungen,
  Header, Auth-Schlüssel und Config-Revision erforderlich. Keine Streams,
  POST/PUT/DELETE oder bekannte schreibende Duet-GET-Endpunkte. Jeder Verbraucher
  behält ein eigenes Handle; Freigabe/Cancel zerstört keine anderen Ergebnisse.
  Maximal die bestehenden vier Ergebnisslots, kein zusätzlicher Responsecache.
- Keine automatische Wiederholung von HTTP- oder Drucker-Schreibbefehlen.
  `retryable` bedeutet nur Eignung eines GET, nicht ausgeführten Retry.
- Google/Microsoft Kalender: bestehender Zeitraum gestern bis +30 Tage,
  maximal vier Folgeseiten/32 Termine, 15-s-Abrufbudget, kontrollierte NextLinks,
  Generation-/Accountprüfung und letzten Cache bei Fehler behalten. Gleiche
  Inhaltschecksummen vermeiden erneutes Schreiben.
- Bestehendes CalDAV/CardDAV: gemeinsames 20-s-Netzwerkbudget je Operation,
  höchstens acht Sekunden je weiterem nativen Request, Prüfung von Konto-
  Generation/Netzwerk vor Folgerequests und in HTTP-Ereignissen. TLS-/Redirect-
  Prüfungen und vorhandene Offline-Daten bleiben erhalten. Kein neuer Provider.

## Speicher und Einstellungen

- Globale Verzeichnissortierung trotz Pagination; erste 96 Einträge benötigen
  nur 97 Retained-Slots statt die gesamte Liste. Spätere Seiten behalten nur
  offset+page+1, maximal 4096 sichtbare Einträge mit verständlicher Limitmeldung.
  Entries liegen in PSRAM; Allocationfehler sichtbar. SD/USB/NAS weiter über
  StorageService/FileBrowserService, Jobs auf bestehendem Transferworker.
- Bestehende Bildpuffer-/Ressourcen-LRU-Grenzen weiterverwendet, kein Zweitcache.
- Medien-Speicherpräferenz via zentraler Config-Revision gecacht, keine wiederholten
  NVS-Lesezugriffe bei unverändertem Stand.
- `apps/vnext_reference` enthält getrennte main.lua/settings.lua und
  web_settings.json. Beide Einstellungen nutzen `display_name` im vorhandenen
  App-KV-Service. UI-Navigation bleibt Queue-basiert.

## Messungen und Grenzen

Abnahme des endgültigen lokalen Quellstands am 10.10.2026:

- `python tools/dev.py test`: 225 Tests OK (171,347 s), anschließend vier
  Architekturprüfungen OK. Kein Test zur Fehlerkaschierung deaktiviert.
- `python tools/dev.py validate`: 225 Tests OK (169,991 s),
  `VALID Home-Display 0.3.3b; config schema 2; production=False`.
- `python tools/dev.py build --env home_display_p4_legacy_dev`: SUCCESS,
  geprüfte pre-v3-Silicon-Konfiguration, P4 360 MHz.
- `git diff --check`: keine Whitespacefehler. HEAD unverändert.

Die zusätzlichen Tests führen Produktionsmethoden bzw. die tatsächliche Lua-VM
mit begrenzten Test-Doubles aus: Desktop-Rückkehr, Lifecycle/LRU/Quarantäne/
Disable, deklarative UI/gezielte Patches/UTF-8/Nodegrenzen, getrennte Referenz-
States, Datenabonnements und verzögertes Profilladen, Job-Owner/Cancel/Release,
GET-Fanout und Authwechsel, keine Command-Replays, HTTP-Teilübertragungen/
Timeouts, Priority-Fairness/Transfer-Isolation, Kalenderseiten/NextLink/Offline/
Generation/OOM/Unchanged-Writes, DAV-Deadline und sortierte 10.000 Einträge.
Dies sind keine echten Socket-, TLS-, NAS- oder Display-Messungen.

Supportbericht: tatsächliche Render-/Flush-Callbackzeiten, FPS, Navigation bis
zum nächsten gezeichneten Refresh, Cold/Warmstart, Read/Compile/Homebuild,
P50/P95/P99, internes Heap/PSRAM, Worker-Warte-/Laufzeit getrennt nach Klasse,
Queue-Peak, Job-Timeouts/Cancel/Coalescing und Retry-Eignung. Messungen im RAM.
Queued-Navigation wird beim Request erfasst, beim späteren Open nicht ersetzt.

Build `home_display_p4_legacy_dev`: RAM 64.584 Byte, Flash 5.213.574 Byte.
Ausgangsbuild: 63.568 / 5.199.326 Byte. Differenz +1.016 Byte statisches RAM,
+14.248 Byte Firmware-Flash. Neue permanente Tasks: 0. Queue-/Slot-Zahlen
unverändert. Dynamische RAM-/PSRAM-Spitzen sind nicht auf Hardware gemessen.

Synchrones DNS und bereits laufende native HTTP-Aufrufe sind nicht präemptiv
abbrechbar. Lua-Instruktionshooks können Compiler oder laufende native Bridge-
Aufrufe nicht unterbrechen. Flush-Callbackzeit ist keine physische DMA-Dauer;
Touchmessung bezieht sich auf Navigation, nicht jede beliebige Widgetaktion.
Keine erfundenen Hardware-Performancewerte, kein C6-Rechen-Offload:
C6 bleibt Funk-Coprozessor, P4-UI/IO-Core-Zuordnung bleibt erhalten.
CalDAV-Discovery und CardDAV-Kontaktabruf bleiben die vorhandenen Funktionen;
ein neuer CalDAV-Terminimport wurde mit diesem Infrastrukturauftrag nicht gebaut.
Die vorhandenen Alt-App-Pakethelfer wurden ebenfalls nicht migriert;
neue Pakete müssen die Schema-3-Referenz verwenden.

Hardwareabnahme bleibt erforderlich: Boot/Recovery/PIN, schneller Appwechsel,
Controlcenter/Wallpaper/Saver, USB/SD/NAS-Verlust, reale langsame/offline Drucker,
Kalender und großer Upload parallel, RAM-Druck und lange Touch-Bedienung.
Kompilation und Hosttests ersetzen diese Prüfung nicht.
