# Gemeinsame Lua-App-Grundlage (Version 1)

Schema-2-Apps verwenden die gemeinsamen APIs, ohne eigene App-Tasks oder
gerätespezifische Bridges. Die alten `hp_http_status/result/cancel`-Namen sind
entfernt: neue Apps verwenden ausschließlich `hp_job_status/result/cancel`.
Alte Pakete müssen gegebenenfalls neu erstellt werden. Beispielpaket:
[`examples/lua-app-template`](../examples/lua-app-template).

## Capabilities und Events

`hp_capabilities()` liefert `version`, API-Versionen in `apis`, `permissions`,
`limits` und `image_formats`. API vorhanden bedeutet nicht Permission erteilt.
WebSocket, MQTT und Drucker-Protokolle werden ausdrücklich **nicht** angeboten.

`hp_events_subscribe({"theme.changed", ...})` wählt bis zu acht exakte
Broadcast-Namen. `{}` deaktiviert Broadcasts, `nil` stellt das bisherige Verhalten
wieder her. Eigene Job-Abschlüsse bleiben immer zustellbar. Der Filter gilt beim
UI-Dispatch, nicht vor der gemeinsamen Queue. `hp_events_status()` liefert den
globalen Drop-Zähler; bei einer Änderung Snapshot und eigene offene Job-IDs
abfragen. `hp_system_snapshot()` liefert gecachte Netzwerk-/Storage-Verfügbarkeit,
Theme-Generation, Idle-Stufe, Uptime und Drops, ohne Verzeichnisse zu scannen.

## Einheitliche Jobs

Bestehende Status-/Result-/Cancel-APIs bleiben erhalten. `hp_job_result(id)`
enthält zusätzlich `state`, `error_code`, `cancelled`, `retryable`, `processed`,
`total`, `queue_ms`, `run_ms`. Originales `error` bleibt unverändert.
`hp_job_release(id)` gibt nur abgeschlossene eigene Ergebnisse frei.

Vier gemeinsame Slots, Resultate maximal 16 KiB, Aufbewahrung 60 Sekunden.
Keine automatischen Wiederholungen von schreibenden Requests. `retryable` ist
nur ein Hinweis für normale HTTP-GETs; die App entscheidet über Backoff.
Cancel ist kooperativ: ein bereits laufender SDK-/Dateisystemaufruf wird nicht
hart unterbrochen. Währenddessen ist `state="cancelling"` beobachtbar. Wie bisher
werden gecancelte Handles nach dem Run entfernt; danach liefern Status/Result
`nil`. Owner-Cleanup ist eine additive Registry (acht Hooks), kein
einzelner Callback, der Ressourcen anderer Module überschreiben könnte.

## App-eigene Dateien als Stream

Alle Funktionen liefern **Job-IDs**, nicht synchron einen Stream-Handle.
Permission: `storage.writeAppData`. Auf einen Job-Abschluss warten, Ergebnis
prüfen und freigeben, erst dann den nächsten Block senden.

| Aufruf | Ergebnis |
| --- | --- |
| `hp_stream_begin(name, expectedBytes)` | `body` enthält dezimal den Handle; `0` Bytes bedeutet unbekannte Länge |
| `hp_stream_write(handle, offset, bytes)` | zusammenhängender Block, `processed` = geschriebene Bytes |
| `hp_stream_commit(handle)` | vollständige Datei übernehmen, `size` = Dateigröße |
| `hp_stream_abort(handle)` | temporäre Datei verwerfen |
| `hp_stream_read(name, offset, length)` | binäres `body`, `processed` = nächster Offset, `total` = Dateigröße |

Gemeinsamer StorageService, eigener Namespace `streams/<app-id>/`, drei Sessions,
Blöcke maximal 8192 Bytes. SD: maximal 16 MiB/Datei und 64 MiB/Owner;
intern: maximal 1 MiB/Datei und Owner. Maximal 32 Dateien, begrenzte Pfadtiefe.
Legacy-Dateien unter `apps/<app-id>/` behalten ihre bisherigen kleineren Quoten.
Komplett-Löschen entfernt beide Namespaces. Der App-Datenexport enthält
committed Stream-Dateien im zusätzlichen JSON-Feld `streams`; Teil-/Backup-Dateien
werden dort nicht exportiert.
Keine fremden App-IDs oder Rohpfade. Fehlerhafte Offsets werden abgewiesen.
Erst Commit ersetzt eine vorhandene Datei über Backup/Rename; Teil-Dateien
werden nie als vollständige Dateien geöffnet. Inaktive Sessions verfallen nach
120 Sekunden. App-Close/Disable/Evict markiert ihre Sessions zur Bereinigung
im vorhandenen Worker. Bei entfernter SD kann eine Teil-Datei zurückbleiben;
beim erneuten Begin desselben Ziels wird sie verworfen, nie übernommen.

`storage.readRange(path, {offset=0,limit=8192})` bzw.
`hp_storage_read_range(...)` liest Medien aus bestehenden SD-/NAS-Providern.
Es gelten unverändert `storage.read`, zusätzlich `storage.nasRead` für NAS und
die bisherigen geschützten Pfadbereiche. `processed` ist der nächste Offset.
Offsets sind derzeit auf 16 MiB begrenzt. NAS bleibt auf vorhandene Provider
beschränkt (kein neues Protokoll, keine Zugangsdaten in Lua).

## HTTP-Dateitransfer

`hp_http_download({url=...,file="images/photo.jpg",timeout=1500,total_timeout=60000})`
lädt direkt in eine app-eigene Stream-Datei. Nur ein vollständig empfangener
2xx-Response wird committed. `hp_http_upload({...})` sendet eine committed Datei
per PUT. Upload-Antworten bleiben auf 16 KiB begrenzt. Chunk-Fortschritt steht in
`hp_job_result`. Netzwerk-Permission und `storage.writeAppData` erforderlich.

Header, Secret-Referenzen, TLS-Prüfung, DNS-Pinning, Ziel-IP-Permissions und
gesperrte Redirects nutzen denselben HTTP-Pfad wie normale Requests.
`timeout` begrenzt blockierende SDK-Aufrufe (250–5000 ms), `total_timeout` die
kooperative Transferdauer (1000–120000 ms). Ein gerade blockierender Aufruf kann
diese Gesamtdauer um seinen Socket-Timeout überschreiten. Keine automatischen
Upload-/Download-Retries, kein kompletter Datei-RAM-Puffer.

Die SDK-Streamingfolge basiert auf der
[ESP-IDF-5.5-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/protocols/esp_http_client.html).
Native Dienste laufen im vorhandenen Worker; Lua und UI-Mutationen verbleiben
im UI-Task. Im Background sind nur die asynchronen Stream-Requests freigegeben.
