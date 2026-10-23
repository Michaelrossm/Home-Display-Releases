# Gemeinsame Energiehistorie

Installierbare Apps bleiben getrennt von der Firmware. Alle folgenden APIs sind
optional und ergänzen die vorhandene EnergyService-/NetworkWorker-Infrastruktur.

## Automatischer Tracker-Import

`hp_energy_sync_history(index)` liefert einen owner-scoped Job, keinen synchronen
Netzwerkzugriff. Er verwendet das aktive IR-Tracker-Profil (1–8) aus dem gemeinsamen
`sources_json` und das Secret `meterN_login` desselben Owners. Berechtigungen:
`energy`, `network.local`, `storage`, `secrets`. Keine fremden App-IDs oder Secrets.

Der vorhandene Transferworker liest `/api/v1/history.csv?range=complete` über die
gemeinsame HTTP-Engine (DNS-/Adressprüfung, keine Redirects, Abbruch, 2500-ms-
Einzelschritte, 120-s-Gesamtbudget). Keine zusätzlichen Tasks. Streaming-Grenze
16 MiB pro Antwort, 64 MiB pro zusammengeführter Archivdatei. Größenfehler sind
sichtbar; es gibt keine Behauptung unbegrenzter Kapazität.

Speicherwahl: SD, andernfalls USB; niemals interner Flash als stiller Ersatz.
Pfad: `/homepanel/data/energy/imports/<owner>/<SHA256>.csv`. Identität beinhaltet
Owner, Host, Profil/Secret-Key und Benutzername, nicht das Passwort. Passwortwechsel
behalten die Historie; andere Konten/Geräte werden nicht vermischt. USB-Zugriffe
verwenden den vorhandenen generation-gebundenen Dateisystemadapter.

CSV bewahrt Zeitstempel, Mittelwert, Minimum, Maximum, Intervallbezug/-einspeisung
und Quellauflösung. Intervallenergie wird nicht als kumulativer Zähler ausgegeben.
Der zeilenweise Merge behält ältere Datensätze, auch wenn sie nicht mehr im
Tracker vorhanden sind. Bei gleichem Zeitstempel gewinnt die feinere Auflösung.
Bei gleicher Auflösung bleiben vorhandene Werte; fehlende Felder dürfen ergänzt
werden. Fehler, unsortierte Antworten, Entfernen des Datenträgers und Abbrüche
promoten niemals eine Teil-Datei. `.bak` dient der Wiederherstellung des atomaren
Dateitauschs; beschädigte bestehende Archive werden nicht automatisch gelöscht.

Die Strom-App stößt den Import selbst an, anschließend alle sechs Stunden und
über die vorhandene Low-Priority-Background-Policy. Bei Fehlern begrenzter Retry;
kein manueller Import-Button nötig. Innerhalb einer Stunde bereits erfolgreich
geschriebene Archive werden nicht erneut übertragen. Ein suspendierter Lua-State
verarbeitet nur Job-Ergebnisse, ohne sichtbares Resume oder UI-Mutation.

## Eigene Display-Aufzeichnung

`hp_energy_bind_source(index)` bindet die primäre Netz-/Auto-Quelle an den
vorhandenen PowerService. Dieser übernimmt Polling, EnergyService-Publikation und
die zentrale Historie; die App fragt dieselbe Live-Quelle dann nicht nochmals ab.
Die Bindung bleibt bei Suspend erhalten und wird bei normalem Owner-Cleanup
deaktiviert. Es gibt nur einen primären Netzzähler. Eine andere App kann eine
aktive Bindung nicht still übernehmen. Netzwerkziele werden mit der vorhandenen
LAN-/DNS-Policy geprüft und für den nativen HTTP-Client auf die validierte IP
festgelegt. Der IR-Tracker-Adapter verwendet dessen Administrator-Anmeldung.

`hp_energy_archive_settings()` liest die zentralen Aufzeichnungsoptionen;
`hp_energy_configure_archive(enabled, minutes)` schreibt sie über einen vorhandenen
Worker-Job. Wertebereich 1–30 Minuten, zentrale System-PIN-Freigabe erforderlich.
Die Einstellungen werden nicht als zweiter App-KV-Zustand dupliziert.

Die vorhandene kompakte Detailablage unterstützt jetzt SD und USB. Importierte
CSV und lokale Detaildateien bleiben getrennt; grobe Importe überschreiben keine
lokalen Minutenwerte. `hp_energy_history(period)` behält den kurzen bisherigen
Cache-Pfad; `hp_energy_history_archive(period)` liest zusätzlich asynchron im
Transferworker. So wartet die Darstellung des letzten Stands nicht auf einen
langen Import. Im Diagramm haben feinere Messwerte Vorrang, lokale Werte bei
gleicher Auflösung ebenfalls. Ohne Import bleibt der bestehende native Cache der
Fallback. Diagrammabfragen sind auf 64 Punkte und 8 s CSV-Scan begrenzt.

Kapazität hängt vom Medium ab. Die vorhandene ausdrücklich eingestellte lokale
Retention wird respektiert; der Import löscht keine alten CSV-Zeilen.
