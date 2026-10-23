# Generische Lua-Hintergrundausführung

Das bestehende Schema-2-Feld `background` bleibt unverändert. Keine Migration:
Ohne optionales `on_background()` ist ein fälliger Lauf ein harmloser No-op.
`hp_background_capabilities()` liefert `version=1`, `lua_callback=true`,
`callback=true`, `max_callback_ms=10` und `instruction_budget=20000`.

Nur bereits geöffnete, anschließend suspendierte Haupt-App-States können laufen
(höchstens drei im vorhandenen LRU). Keine Initialisierung kalter/evicted Apps,
kein Settings-State, kein Reparenting oder sichtbares Resume. Der vorhandene
UI-Dispatcher wird an den bei `UiShell::begin()` registrierten Task gebunden;
ein fremder Task darf die Background-Methode nicht ausführen.

Der Scheduler wählt höchstens einen fälligen Lua-Owner und setzt lediglich einen
Handoff. Native Firmware-Handler behalten ihren bisherigen Worker-Pfad. Lua läuft
nur unter dem vorhandenen Display-Guard: `Suspended → BackgroundExecuting → Suspended`.
Timer und UI bleiben geparkt. Low/Normal bei High und `requiresNetwork` bei Offline
werden verschoben, nicht per Busy-loop nachgestartet. Keine neuen Tasks oder Worker.

```lua
function on_background()
  request = hp_http_get({url="https://example.com/data"})
end

function on_event(name, event)
  if name == "http.completed" and event.job == request then
    local result = hp_job_result(event.job)
    -- Daten im Lua-State bearbeiten / weitere owner-scoped Jobs starten.
    -- UI erst bei einem späteren sichtbaren Resume aktualisieren.
  end
end
```

Die bestehenden Permission-Checks und Owner-Namespaces gelten weiterhin.
Background erlaubt Async-HTTP, Async-Dateizugriff, KV-/Secret-Schreibjobs,
Job-Status/Result/Cancel, JSON, `hp_millis` und die Background-APIs.
Andere `hp_*`-Bridges sind während des Laufs gesperrt, insbesondere LVGL,
Navigation, PIN-Prompts und synchrone KV-/Secret-Lesezugriffe. Bereits in Lua
gecachte Bridge-Funktionen bleiben geschützt. Owner-Completions können über
`on_event(name,{job=...})` auch an eine unsichtbare App gehen. Maximal ein
Background-Callback pro UI-Pass. Ein ausstehender Scheduler-Handoff hat Vorrang,
damit schnelle Completion-Ketten andere Owner nicht dauerhaft blockieren.

Lastbedingt verschobene Completions behalten ihr Alter, werden frühestens nach
einer Sekunde erneut versucht und verfallen passend zum Result-Cache nach 60 s.
Disable, Evict, Uninstall und Policy `none` entfernen Pending Runs/Policies,
canceln Jobs und verwerfen Owner-Ereignisse. Eine verspätete Worker-Notification
wird zusätzlich anhand der noch gültigen Job-ID und Owner-Zuordnung geprüft.

## Budget und Grenzen

20.000 VM-Instruktionen, Hook alle 100 Instruktionen, Deadline 10.000 µs,
Zeitprüfung vor/nach erlaubten C-Bridges. Fehler/OOM/Budgetende beenden nur diesen
Lauf; kein Quarantine, Close oder Zerstören der sichtbaren App. Hook und Stack
werden wiederhergestellt. `pcall` kann einen Budgetfehler nicht dauerhaft schlucken.

Coroutine-Ausführung, `xpcall`, `print`, dynamisches `load`, explizite GC,
Pattern-Matching und große native String-/Tabellenoperationen sind im Hintergrund
gesperrt. Im Vordergrund bleiben diese Funktionen inklusive Yield-Semantik verfügbar.

Die Deadline ist kooperativ: Der Lua-Hook kann einen gerade laufenden nativen
C-Aufruf, implizite GC oder eine RTOS-Unterbrechung nicht präemptiv stoppen.
Eine mathematisch harte Wallclock-Garantie von ≤10 ms ist deshalb nicht gegeben;
ein überzogener erlaubter C-Aufruf wird direkt danach als Budgetfehler beendet.
Keine Sockets/TLS oder Hardware-Wallclock-Zeiten durch Hosttests validiert.
