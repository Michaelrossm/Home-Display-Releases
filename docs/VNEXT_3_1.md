# Firmware VNext 3.1 – lokale Performance-Finalisierung

## Architektur und API

Die bestehende Lua-Engine, UI-Queue, NetworkWorker/Transferworker und Services
werden weiterverwendet. Keine neuen permanenten Tasks, kein zweiter Renderer,
keine App-Migration und keine Änderung am Manifest-Schema 3. C6 bleibt der
WLAN-Coprozessor; der geprüfte Legacy-Build verwendet 360 MHz.

- `ui.insert(parentId, index, spec)`, `ui.remove(id)` und
  `ui.updateChildren(parentId, specs)` verwenden stabile IDs. Gleicher Typ und
  Parent erhalten das vorhandene Objekt. Grenzen: 256 IDs, 16 Ebenen; ein
  Parentwechsel erfolgt durch Entfernen und Einfügen, nicht durch Reparenting.
  Entfernte Callback-Bindungen werden freigegeben; alte Callback-Namen bleiben
  wirkungslos. `ui.patch` fasst Änderungen zusammen und überspringt gleiche Werte.
- `virtual_list`: maximal 24 wiederverwendete Zeilen, Seiten bis 96 Einträge.
  `ui.setItems(id, offset, items, total)` verwendet einen nullbasierten Offset;
  Einträge besitzen `id`, `text` und optional `enabled`. `on_scroll` fordert eine
  Seite an; `on_select` liefert die stabile ID und den Index. Status über
  `ui.patch(id, {status='loading'})`, entsprechend `ready/empty/offline/error`.
  Auch 10.000 Einträge erzeugen keine 10.000 LVGL-Objekte.
- `month_grid`: 42 wiederverwendete Tageszellen.
  `ui.setMonth(id, {year=2026, month=10, week_start='monday', today='2026-10-11',
  selected='2026-10-11', days={}})`; `sunday` wird ebenfalls unterstützt.
  Tagesdaten enthalten Datum, Anzahl und maximal vier Markerfarben.
  `on_select` liefert das ISO-Datum. Monate und Schaltjahre werden nativ geprüft.
- `ui.chartSeries(id, index, values, {name='Netz', color=0x44aaff})` unterstützt
  vier Reihen mit maximal 256 Punkten einschließlich negativer Werte.
  Reihenname/Farbe bleiben nach Erzeugung bis zur Neuerzeugung konstant.
  Native API: `hp_chart_resize`, `hp_chart_append` und
  `hp_chart_history_range`; maximal 1.024 Samples je Reihe, begrenzte
  Min/Max-Verdichtung erhält Spitzen. Ältere persistierte Energiedaten bleiben
  über den bestehenden `hp_energy_history`-Service verfügbar; keine neue
  Drucker-Historienpersistenz und kein zweiter Datenprovider.
- `hp_app_memory()` zeigt Lua-/Native-/Objektverbrauch, Spitzen, Grenzen und
  Budgetfehler. Native appgebundene Strukturen: maximal 512 KiB; Lua-Budget und
  bestehende ImageResource-Grenzen bleiben getrennt erhalten.

## UI und Datenpfad

Der Erstaufbau bleibt kooperativ auf acht Widgets beziehungsweise zwei
Millisekunden begrenzt; einzelne native LVGL-Operationen sind nicht präemptiv.
Sichtbare Navigation hat Vorrang vor Daten, Charts und Bildern; Aging verhindert
Verhungern. Die bestehende UI-Schleife wird für ausstehende Arbeit geweckt.
`ui.meaningful()` unterscheidet bedienbaren Erstinhalt von `ui.ready()` für den
vollständigen Strukturaufbau. Netzwerkdaten müssen dafür nicht fertig sein.
`ui_first_meaningful` misst den tatsächlich gerenderten Frame, nicht nur den
Abschluss der Lua-Erzeugung.

Provider-Revisionen verhindern unveränderte JSON-Abfragen und Decodierungen.
Zeitstempel, Loading und Offline werden unabhängig vom Dateninhalt geliefert;
nur betroffene Abonnenten werden benachrichtigt. Berechtigungen und
Profil-/Owner-Isolation bleiben bestehen.

Kalender lädt angeforderte Zeiträume über bestehende Hintergrundjobs paginiert.
Grenzen: 128 Cloud-Ereignisse, 16 Seiten, 15 Sekunden; unvollständige Ergebnisse
werden ausdrücklich markiert. Bei großen Kalendern den Zeitraum eingrenzen.
Es gibt weiterhin ein begrenztes Cloud-Fenster pro Konto, keinen unbeschränkten
Mehrfenster-Cache. Alte 32-Ereignis-Dateien bleiben lesbar; neue Detaildateien
speichern nur tatsächlich belegte Einträge. Die UI wartet nicht auf HTTP.

Der gemeinsame Datei-LRU hält vier Seiten mit je 100 Einträgen. Schlüssel
enthalten Quelle, vollständigen Pfad, Sortierung, Filter und Offset;
Storage-/USB-/NAS-Revisionen invalidieren alte Ergebnisse. TTL: lokal zehn,
NAS zwei Sekunden. Sortierung benutzt begrenzte Fenster statt einer mit dem
Offset wachsenden Liste. Grenzen und Deadline werden als Fehler angezeigt,
nicht als scheinbar vollständige Liste. Unbekannter NAS-Verbindungszustand
kann bis zum TTL noch gecacht sein. Bilder starten ihren bestehenden
Transferjob erst bei sichtbarem Widget; geparkte Apps bleiben unsichtbar.

## Schnellleiste und Partitionen

Langes Drücken aktiviert den Bearbeitungsmodus. Nur dann erscheinen die
Entfernen-Symbole; „Fertig“ beendet ihn. Ziehen und gespeicherte Verknüpfungen
verwenden den bisherigen Controller. Entfernen löscht nur die Verknüpfung,
nicht die App.

Die Partitionen sind jetzt OTA 2 × 9 MiB, AppFS 9 MiB und Systemdata
4,8125 MiB. **Kein normales OTA über die alte Partitionstabelle durchführen.**
Offsets haben sich geändert. Vor einem vollständigen Flash Daten exportieren
und anschließend kontrolliert migrieren; siehe [PARTITIONS.md](PARTITIONS.md).
Es wurde weder geflasht noch automatisch formatiert.

## Verifikation und Ressourcen

Ausgangsstand: 64.848 Byte statischer RAM, 5.222.494 Byte ELF-Flashbelegung.
Geprüfter Legacy-360-MHz-Endstand: 65.104 Byte statischer RAM (+256 Byte),
5.264.300 Byte ELF-Flashbelegung (+41.806 Byte). Dauerhafte Task-Differenz: null.
P4-DWARF-Strukturgrößen, ausdrücklich keine gemessenen Laufzeitspitzen:

| Struktur | Reservierter Speicher |
| --- | ---: |
| Globaler Dateiseiten-Cache, bei Bedarf | 55.432 Byte PSRAM |
| Virtual List, je Widget | 37.460 Byte PSRAM |
| Monatsraster, je Widget | 2.832 Byte PSRAM |
| Chart-Historie, je genutzter Reihe | 16.392 Byte PSRAM |
| Maximaler Cloud-Detail-Snapshot | 683.032 Byte PSRAM |

Hosttests prüfen keyed Wiederverwendung, stale Callbacks, 10.000 Listeneinträge,
Monatswechsel/Schaltjahre, Priorisierung, Revisionen, Ringpuffer-Verdichtung,
Dateiseiten-LRU und Partitionen. Kalender-Provider-Tests prüfen echte
Mehrseitenabfragen, Grenzen, Abbruch und bestehende Sicherheitsregeln.

Test-Endwerte stehen im Abschlussbericht. Ein erfolgreicher Build ist
keine Hardwarevalidierung. Touch-Latenz, FPS, Laufzeit-PSRAM-Spitzen und
First-Frame-Zeiten müssen am Gerät gemessen werden; hierfür werden keine
Performancewerte erfunden. Keine Git-Commits oder Pushes.
