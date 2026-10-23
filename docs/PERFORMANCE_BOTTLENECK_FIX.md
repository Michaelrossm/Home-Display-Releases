# Worker-, Theme- und Energiesparpfade

## Ausführungsklassen

Die bestehenden NetworkWorker-Tasks bleiben erhalten. Generisches HTTP mit
mehr als 750 ms Timeout, Streams, Discovery, Druckvorschauen und Kalender-
Netzwerkaufträge verwenden die Transferklasse. Reservierte Druckerbefehle
bleiben in der priorisierten kurzen Klasse; es entstehen keine Retries oder
Befehlswiederholungen. Eine laufende synchrone Anfrage ist weiterhin nicht
präemptierbar. Auch die Transferklasse ist begrenzt und serialisiert; große
Uploads können dort einen OAuth-/Download-Auftrag verzögern, nicht jedoch
die separate Befehlswarteschlange. Fehlgeschlagene automatische Sync-
Einreihungen werden nach fünf Sekunden statt erst nach fünf Minuten versucht.

`submitLocal` und `submitLocalTransfer` verwenden dieselben Worker ohne
Radio-Burst. Nur als Netzwerkauftrag deklarierte Jobs und explizite HTTP-
Aktivität heben den WLAN-Sparmodus auf. Queue-Zähler werden vor Übergabe
erhöht und bei Ablehnung bzw. Übernahme ausgeglichen.

## Themes

Der initiale Theme-Scan erfolgt vor Display/LVGL-Initialisierung. Laufzeit-
Scans werden als lokale I/O-Arbeit eingereiht. Der UI-Lock schützt nur die
Veröffentlichung eines fertigen, begrenzten Snapshots; keine Theme-Datei
wird dabei geöffnet. Bis zum nächsten Aufruf von `refreshSd()` nach
Fertigstellung bleibt die vorherige Auswahl verfügbar. Kein zusätzlicher Task.

## Erinnerungen

Lokale Erinnerungen verwenden nicht mehr die Upload-Warteschlange. Nach
einem vollständigen Scan wird nur der nächste Fälligkeitstermin gespeichert,
maximal fünf Minuten. Die 15-Sekunden-Prüfung bleibt bestehen, benötigt bei
Cache-Treffern aber keine Event-Dateizugriffe. Revision, Datenschutzstatus,
Uhr-Rücksprung, Scanfehler und volle Erinnerungsslots verhindern eine
unzulässige Verschiebung. An Record-Grenzen wird für anstehende priorisierte
Aufträge abgebrochen und später erneut gescannt.

## CPU-/Display-Schutz

DisplayManager besitzt die CPU-Maximalsperre. Sichtbares Bild, einschließlich
gedimmtem Display und Always-On, behält die bisherige Render-Taktgarantie.
Erst nach erfolgreichem Backlight-Aus wird sie freigegeben. Vor Backlight-
Ein wird die Sperre erneut erworben. Fehlgeschlagene Hardwareänderungen
werden nicht als erfolgreich gemeldet. Andere PerformanceGuard-Nutzer
behalten ihre Sperren. Light-Sleep und Display-/Bufferkonfiguration bleiben
unverändert. Reales Aus-/Ein-Verhalten muss am Gerät verifiziert werden;
Build und Hosttests sind keine Hardware- oder Verbrauchsmessung.

## Nachweise

`test_performance_bottlenecks.py` führt echten Worker- und Backlight-Code
gegen kontrollierte Host-Fakes aus: Radio-Ruhe für lokale Arbeit, Queue-
Ablehnung ohne Zählerleck, Klassifizierung langsamer Anfragen, kein SD-I/O
im Refresh und Sperrerwerb vor Aufwecken. Native Kalender-Tests prüfen
Cache-Treffer ohne neue Visits, Fälligkeit, Revision/Fehler und mehr
gleichzeitig fällige Erinnerungen als UI-Slots. Keine erfundenen P95-/Wattwerte.
