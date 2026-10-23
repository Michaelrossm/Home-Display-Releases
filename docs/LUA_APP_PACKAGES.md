# Lua-App-Pakete (Schema 2)

Neue Pakete verwenden Schema 2. Alle Apps teilen dieselbe Runtime, Navigation,
App-Verwaltung und die nativen Dienste. Apps dürfen keine eigenen Netzwerk-Tasks
oder Hardwaretreiber erstellen. Die öffentliche API steht in [LUA_APP_API.md](LUA_APP_API.md).

Zusätzliche generische Medien-Capabilities: `storage.read`,
`storage.writeAppData` (Alias `storage`, nicht gleichzeitig angeben),
`storage.nasRead`, `system.wallpaper`, `power.idleIntegration`.
Grenzen und asynchrone APIs: [MEDIA_STORAGE_API.md](MEDIA_STORAGE_API.md).

## Minimales Paket ohne Einstellungen

ZIP-Hauptverzeichnis: `app.json`, `main.lua`. Keine Unterordner, keine
`settings.lua`, keine zusätzlichen nativen Module.

```json
{"schema":2,"id":"calculator","name":"Taschenrechner","version":"2.0.0",
 "type":"lua","entry":"main.lua","settings":false,"permissions":[],"icon":"calculator"}
```

```lua
function on_open()
  local title = hp_label(hp_root(), "Taschenrechner")
  hp_align(title, "top_left", 24, 24)
end
```

Auch ohne eigene Einstellungen erscheint jede installierbare App mit einem
Ein/Aus-Schalter in System-Einstellungen → Apps. Deaktivierte Apps bleiben dort
sichtbar, sind aber weder über Launcher/Dock noch über die Navigation startbar.

## Netzwerk-App mit getrennten Einstellungen

```json
{"schema":2,"id":"meter","name":"Messgerät","version":"2.0.0","type":"lua",
 "entry":"main.lua","settings":true,"settingsEntry":"settings.lua","icon":"energy",
 "permissions":["network.local","storage","secrets","discovery","energy"]}
```

`settings` muss Boolean sein. Mit Einstellungen ist ausschließlich
`settingsEntry:"settings.lua"` erlaubt; Datei und Lua-Syntax müssen gültig sein.
Ohne Einstellungen darf weder settingsEntry noch settings.lua vorhanden sein.
Unbekannte/doppelte Permissions werden abgewiesen. Permissions sind deklarierte
Capabilities, keine pauschale Freigabe aller nativen Bridges.

`main.lua`:

```lua
local status, job
function on_open()
  status = hp_label(hp_root(), "Wird geladen")
  job = hp_http_get({url="http://192.168.178.50/api/status", timeout=1500})
  if not job then hp_set_text(status, "Worker ausgelastet") end
end
function on_event(name, data)
  if name == "http.completed" and data.job == job then
    local r = hp_job_result(job)
    if r and r.done then
      hp_set_text(status, r.ok and r.body or r.error)
      hp_job_cancel(job) -- Ergebnis freigeben
    end
  end
end
function open_preferences() hp_open_app_settings() end
```

`settings.lua` hat einen frischen Lua-State:

```lua
function on_open()
  local root = hp_root()
  local hint = hp_label(root, "Einstellungen dieser App")
  hp_align(hint, "top_left", 24, 24)
  local back = hp_button(root, "Zur App")
  hp_align(back, "top_left", 24, 90)
  hp_on_click(back, "return_to_app")
end
function return_to_app() hp_open_app() end
```

Globale Variablen aus main.lua sind hier nicht vorhanden. Dauerhafte Daten
gehören in den eigenen KV-/Secret-Namespace, nicht in globale Variablen.
Beide Navigationen verwenden ausschließlich UiShells gemeinsame Queue; der
Wechsel erfolgt später unter dem bestehenden DisplayUiGuard.

## Installation und Fehlerbehandlung

ID: 1–31 Zeichen aus a–z, 0–9, _ und -. Name maximal 48, Version maximal
24 Zeichen. Typ lua, Einstieg main.lua. Optional minFirmware bleibt unterstützt.
Das Web-Installationsformular und der native Installer prüfen Schema, Dateien,
Settings und Permissions. Lua-Syntax wird im Worker mit 1 MiB Validator-Budget
geprüft. Der vorhandene Stage/Backup/Commit/Rollback-Pfad bleibt erhalten; eine
fehlgeschlagene Validierung ersetzt niemals die laufende Version.

Schema 1 bleibt lesbar, erhält aber **keine impliziten Berechtigungen**.
Bestehende Fitz Touch 1.3.1 und weitere Schema-1-Pakete mit Media/Storage/Netzwerk
müssen vor produktiver Nutzung auf Schema 2 und die benötigten Permissions
umgestellt werden. Die vorhandenen App-Skripte und ZIP-Dateien wurden bei diesem
Plattformumbau bewusst nicht migriert oder neu verpackt.

Die alten hp_kv_get/hp_kv_set werden nicht mehr registriert. Typisierte Setter,
Dateioperationen und Secrets-Mutationen sind nun asynchron und liefern Job-IDs.
Radio-Speichern ist ebenfalls queued, nicht mehr eine sofortige Persistenzbestätigung.
Radio-PINs werden nicht mehr an Lua zurückgegeben; ein leerer PIN beim Speichern
behält den vorhandenen nativen PIN.
