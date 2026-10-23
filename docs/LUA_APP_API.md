# Lua App API — Schema 2

## Laufzeit und gemeinsame Regeln

Capability-Abfrage, gefilterte Events, Job-Metadaten und Datei-/HTTP-Streaming:
[LUA_PLATFORM_API.md](LUA_PLATFORM_API.md). Eine getestete generische Vorlage
liegt unter [examples/lua-app-template](../examples/lua-app-template).

Die generischen SD-/NAS-, Bild-, Thumbnail-, Wallpaper- und Inaktivitäts-APIs
sind in [MEDIA_STORAGE_API.md](MEDIA_STORAGE_API.md) dokumentiert. Sie ergänzen
Schema 2 und verwenden dieselbe owner-scoped Job-Queue; bestehende APIs bleiben
unverändert. Das bisherige kleine Lua-Asset-Budget bleibt separat bestehen.

`main.lua` und `settings.lua` verwenden jeweils einen frischen, getrennten State.
Callbacks: `on_open()`, optional `on_tick()`, `on_close()`, `on_event(name,data)`.
Native `pause()/resume()` unterstützen optional `on_pause()/on_resume()`; normale
App-Wechsel schließen den alten State statt eine zweite App im Hintergrund zu halten.
Es gibt derzeit keinen Lua-Aufruf, der Apps selbst pausiert.

Lua-Speicher: maximal 1 MiB pro State, PSRAM bevorzugt. Skript maximal 192 KiB.
UI-Konstruktionsbudget 1024, Callback-Bindings 256, Bilddaten zusätzlich 512 KiB
pro State. Diese Budgets werden erst bei close zurückgesetzt: Widgets wiederverwenden.
Callbacks maximal 30 ms/~500k Instruktionen, Start 100 ms/~2M, Tick/Events
12 ms/~120k. Langsame Ticks werden von 750 ms auf 1200/2000 ms zurückgenommen;
fehlerhafte Ticks werden pausiert. Native Aufrufe sind nicht präemptierbar.

Kein io/os/debug/package, require/dofile/loadfile oder native Modul-Loading.
UI-Handles dürfen nur im eigenen View benutzt werden. LVGL bleibt BSP-owned;
kein zweiter Handler/Displaytask. UI-Events verwenden unabhängig voneinander
gebundene Funktionsnamen, nicht mehr gemeinsames Objekt-user_data.

Alle unten genannten UI-, JSON-, Zeit- und Navigationsfunktionen brauchen keine
Permission. Media/Radio brauchen `media`, Energie `energy`, KV/Dateien `storage`,
Secrets `secrets`. HTTP benötigt je nach aufgelöstem Ziel `network.local` oder
`network.internet`; Discovery benötigt **discovery und network.local**.
Nicht erlaubte Bridges werden gar nicht registriert.

## Jobs, HTTP und Events

Vier gemeinsame Job-Slots benutzen den vorhandenen NetworkWorker. Kein neuer
Task. Job-IDs bleiben im positiven 32-Bit-Bereich. Mutationen/Dateioperationen
liefern Job-ID oder nil, nicht die sofortige Bestätigung einer Speicherung.
Ergebnisse verfallen nach 60 Sekunden, expliziter cancel gibt sie frei.
App-close cancelt ihre Jobs. Abbruch ist kooperativ: ein bereits laufender
Socket-Aufruf endet spätestens mit seinem nativen Timeout; DNS benutzt lwIPs
Resolver-Timeout, nicht den HTTP-Timeout. Keine UI-Warteoperation.

| Signatur | Rückgabe / Argumente | Permission / Beispiel |
|---|---|---|
| `hp_job_status(id)` | `"pending"`, `"done"` oder nil (unbekannt/Lock gerade belegt) | Eine Job-Capability; `local s=hp_job_status(id)` |
| `hp_job_result(id)` | `{done,ok,status,body,error,size}` oder nil | Wie oben; `local r=hp_job_result(id)` |
| `hp_job_cancel(id)` | Boolean; kein Zugriff auf Jobs anderer Apps | Wie oben; `hp_job_cancel(id)` |
| `hp_http_get(options)` | Job-ID/nil | LAN/WAN; `hp_http_get({url="http://192.168.1.20/status"})` |
| `hp_http_post(options)` | Job-ID/nil, optional body | LAN/WAN; `hp_http_post({url=url,body="{}",content_type="application/json"})` |
| `hp_http_put(options)` | Job-ID/nil | LAN/WAN; `hp_http_put({url=url,body="value"})` |
| `hp_http_delete(options)` | Job-ID/nil | LAN/WAN; `hp_http_delete({url=url})` |
| `hp_network_status()` | `{online,lan,wifi,ip}` | Keine; `local n=hp_network_status()` |

HTTP options: `url` maximal 1024, `timeout` 250–5000 ms (Default 1500), `body`
maximal 8192 Byte, `content_type` maximal 128, `headers` maximal acht
String-Paare (Namen 64, Werte 512). Response maximal 16384 Byte. Methoden
GET/POST/PUT/DELETE. Nur http/https und IPv4, keine Credentials in URL,
Weiterleitungen, IPv6, Loopback, Link-local oder Multicast. DNS erfolgt im
Worker; die geprüfte IP wird für die Verbindung festgelegt. HTTPS prüft
Zertifikat/Originalhost über die vorhandene CA-Bundle-Infrastruktur.

`bearer_secret="token"` oder `basic_auth_secret="login"` braucht zusätzlich
secrets; Basic-Secret enthält `username:password`. Keine freien Authorization-,
Host-, Content-Length-/Transfer-Encoding-Header. CR/LF-Injection wird verworfen.
WAN-Erreichbarkeit wird nicht aus Link-Up geraten: `internet` ist momentan
nicht vorhanden/unknown. `status` ist bei HTTP der Response-Code; `ok` gilt nur
bei 2xx ohne Fehler. Ergebnisse fremder Apps werden niemals ausgegeben.

`on_event(name,data)` wird nur im guarded UI-Kontext aufgerufen. Bounded Queue
mit 16 Einträgen, maximal vier Callbacks pro UI-Pass. Bei Überlast können Events
verloren gehen; Job-Status bleibt als Fallback abfragbar. `data.job` bezeichnet
den Job. Vorhandene Producer: network.connected/disconnected/changed,
time.minute/day_changed, http.completed, storage.changed, discovery.completed,
energy.updated/history, media.updated (queued Media-Kommando), native.completed
(Radio-Speichern), theme.changed. Native Module können `publishLuaEvent`
verwenden. Es existiert noch keine allgemeine Event-Payload für MQTT/WS-Nachrichten.

```lua
function on_event(name,data)
  if name=="http.completed" then
    local r=hp_job_result(data.job)
    if r and r.done then print(r.status) end -- Keine Credentials loggen!
    hp_job_cancel(data.job)
  end
end
```

## JSON

| Funktion | Argument / Rückgabe / Beispiel |
|---|---|
| `hp_json_decode(text)` | Lua-Table/Skalar oder nil,error; `local x=hp_json_decode('{"w":-120}')` |
| `hp_json_encode(table)` | String oder nil,error; `local s=hp_json_encode({w=-120})` |

Maximal 32 KiB Dokument, 64 KiB nativer JSON-Allocator (PSRAM), Tiefe 12,
512 Elemente, Strings 8192 und Schlüssel 1024 Byte. Zyklen, nicht-endliche
Zahlen und gemischte/sparse Arrays werden verworfen, nicht still abgeschnitten.
Optionales zweites Argument bei Encode und Decode: `{max_elements=1024}`.
Das Budget muss eine ganze Zahl von 1 bis 2048 sein; ohne Angabe bleiben es
512 Elemente. Byte-, String-, Tiefen- und PSRAM-Allocatorgrenzen bleiben bestehen.
Erkennbar durch `hp_capabilities().apis.json_budget == 1`.

Die deklarative Komponente `type="weather"` verwendet den bestehenden
`UiAmbientAnimationService`, keinen eigenen Task oder App-Timer. Maximal 16
Effekte pro App und 32 systemweit, z. B. `{id="sky",type="weather",weather_type="rain",
width=96,height=64,intensity=35,animated=true}`. `animated=false` stoppt ihn;
verborgene, suspendierte und ausgeschaltete Ansichten werden zentral pausiert.
Die Erstellung und Aktualisierung erfolgt ausschließlich über den UI-Pump.
Jeder Effekt reserviert zehn UI-Objekte und gibt diese beim Löschen wieder frei.
Mehrere sichtbare Effekte teilen denselben Timer (125 ms statt je einem Timer).
Kleine Effekte sind ab 32 x 28 Pixel möglich. Verborgene Karten pausieren.
Capability: `hp_capabilities().apis.weather_widget == 1`.
Mehrfach-Effekte und Vektor-Symboltasten sind gesondert durch `apis.weather_multi`
und `apis.icon_buttons` (jeweils `1`) erkennbar; das alte Wetter-Widget bleibt kompatibel.
Statische farbige Wettergrafiken nutzen denselben Icon-Renderer:
`{id="sky",type="icon",text="rain",width=48,height=48}`. Mit `ui.patch`
kann der Wettertyp (z. B. `clear`, `cloudy`, `rain`, `snow`, `storm`) ohne
Objektneubau gewechselt werden. Ein statisches Icon besitzt keinen Timer.
`type="icon_button",text="drop"` erstellt eine Schaltfläche mit Vektorsymbol
statt Schriftglyph; `on_click`, `primary`, `enabled` arbeiten wie bei Buttons.
`rain_drops` ist ein Drei-Tropfen-Symbol, `radio` ein farbiges Radio.
Diagramme akzeptieren optional `min=0,max=100` als gemeinsam gesetztes,
ganzzahliges Wertepaar (deklarativ -4096 bis 4096). Ohne dieses Paar bleibt
die automatische Skalierung aktiv. Änderungen werden im UI-Pump über
`hp_chart_set_range(chart,min,max)` angewendet; diese Bridge ist owner- und
Foreground-geschützt. Beispiel: Prozentdiagramm mit festem Bereich 0–100.
`ui.chartCursor(id,labels,suffix)` aktiviert optional eine per Touch verschiebbare
vertikale Markierung für die erste Serie. Bis zu 32 Labels (je 32 Bytes),
Suffix bis 16 Bytes. Labels müssen zu den Datenpunkten passen; fehlende Werte
werden als „Keine Daten“ statt als Null angezeigt. Der native Handler läuft
im bestehenden LVGL-Task, ohne Lua-Callback oder zusätzlichen Timer.
Capability: `apis.chart_cursor == 1`; native Bridge `hp_chart_set_cursor_labels`.
Für Eingabefelder setzt `password=true` den nativen Passwortmodus im UI-Pump.
Vor Passwortformularen `apis.password_inputs == 1` prüfen; Werte ausschließlich
mit den vorhandenen Secret-Bridges speichern, nie als ungeschützten KV-Fallback.
Launcher-Manifestnamen `weather` und `printer` erhalten passende Vektorsymbole
in Desktop und Schnellleiste; unbekannte Namen werden nicht app-ID-spezifisch erkannt.
Leere Table wird Objekt. `hp_json_null` ist der Sentinel für JSON null,
auch in Arrays; `hp_json_encode({1,hp_json_null,3})`. Lua-Nummern haben die
Genauigkeit der gepinnten Lua-Konfiguration, keine garantierte 64-Bit-Integer-Exaktheit.

## Typisierter App-Speicher und Secrets

Namen: 1–48 alphanumerische Zeichen, _ und -. Eigene App-ID wird intern benutzt.
NVS-Digest-Schlüssel sind höchstens 15 Byte; vollständige owner/name-Identität
wird gespeichert und geprüft, auch bei Hash-Kollisionen. 64 KV-Namen und
16 Secrets pro App. Setter/Delete liefern Job-ID/nil. Ein Getter kann vor dem
zugehörigen storage.changed-Event noch den alten Wert liefern.

| Signatur | Rückgabe / Beispiel | Permission |
|---|---|---|
| `hp_kv_get_string(name,default="")` | String; `local host=hp_kv_get_string("host","")` | storage |
| `hp_kv_set_string(name,value)` | Job-ID/nil, String maximal 2048; `hp_kv_set_string("host","device.local")` | storage |
| `hp_kv_get_int(name,default=0)` | Integer; `local n=hp_kv_get_int("count",0)` | storage |
| `hp_kv_set_int(name,value)` | Job-ID/nil; `hp_kv_set_int("count",12)` | storage |
| `hp_kv_get_bool(name,default=false)` | Boolean; `local x=hp_kv_get_bool("compact",false)` | storage |
| `hp_kv_set_bool(name,value)` | Job-ID/nil; `hp_kv_set_bool("compact",true)` | storage |
| `hp_kv_get_float(name,default=0)` | Nummer; `local x=hp_kv_get_float("factor",1)` | storage |
| `hp_kv_set_float(name,value)` | Job-ID/nil, endlich; `hp_kv_set_float("factor",1.5)` | storage |
| `hp_kv_exists(name)` | Boolean; `if hp_kv_exists("host") then end` | storage |
| `hp_kv_delete(name)` | Job-ID/nil; `hp_kv_delete("host")` | storage |
| `hp_secret_set(name,value)` | Job-ID/nil, maximal 2048; `hp_secret_set("token",typedToken)` | secrets |
| `hp_secret_exists(name)` | Boolean; `local exists=hp_secret_exists("token")` | secrets |
| `hp_secret_delete(name)` | Job-ID/nil; `hp_secret_delete("token")` | secrets |

Es gibt **kein hp_secret_get**. Native HTTP-Dienste lesen ausschließlich Secrets
ihres Auftraggebers. Speicherung über ConfigServices dedizierte interne
Credential-NVS, niemals SD oder normale App-KV. Wichtig: im bestehenden
sdkconfig ist NVS-/Flash-Verschlüsselung deaktiviert. Dieser Umbau aktiviert
keine eFuse-/Secure-Boot-/Flash-Encryption-Konfiguration und bietet daher keinen
Schutz gegen physisches Auslesen des Flashs. Das ist eine offen dokumentierte
Gerätesicherheitsaufgabe, keine zugesicherte At-Rest-Verschlüsselung.

## App-Dateien (alle asynchron, storage)

| Signatur | Job-Ergebnis / Beispiel |
|---|---|
| `hp_file_read(path)` | body=Dateiinhalt, size=Länge; `local j=hp_file_read("cache.json")` |
| `hp_file_write(path,text)` | ok,size; `hp_file_write("cache.json","{}")` |
| `hp_file_append(path,text)` | ok,size, atomarer Ersatz; `hp_file_append("log.txt","line\n")` |
| `hp_file_exists(path)` | ok=true, body="true"/"false"; `hp_file_exists("cache.json")` |
| `hp_file_delete(path)` | ok; fehlende Datei gilt als gelöscht; `hp_file_delete("cache.json")` |
| `hp_file_size(path)` | ok,size, body=dezimaler String; `hp_file_size("cache.json")` |

Nur relative Pfade im eigenen `apps/<id>/` unter StorageServices UserData-Root.
SD bevorzugt, interner begrenzter Fallback. Pfad maximal 96 Zeichen/drei
Unterordner; keine absoluten Pfade, .., Backslash, Doppel-Slash, Doppelpunkt,
Kontrollzeichen oder reservierte .tmp/.bak-Namen. Keine Verzeichnislöschung.
Maximal 32 Dateien und insgesamt 256 KiB auf SD bzw. 32 KiB intern. Einzeldatei
16 KiB SD/8 KiB intern; Write/Append-Argument maximal 8 KiB. Quota/FS-Arbeit
und atomare Writes laufen ausschließlich im Worker.

## Discovery und Zeit

| Signatur | Argument / Rückgabe / Beispiel | Permission |
|---|---|---|
| `hp_discovery_start(services)` | Job-ID/nil, maximal vier _service._tcp.local; `hp_discovery_start({"_shelly._tcp.local"})` | discovery + network.local |
| `hp_discovery_results(id)` | nil bis fertig, dann bis acht `{service,hostname,host,port}` | wie oben |
| `hp_time_epoch()` | UTC-Epoch; `local now=hp_time_epoch()` | keine |
| `hp_time_local()` | nil bei ungültiger Zeit, sonst year/month/day/hour/minute/second/weekday(1–7)/epoch | keine |
| `hp_timezone()` | konfigurierte POSIX-TZ; `local zone=hp_timezone()` | keine |
| `hp_format_time(epoch,pattern)` | String, Format maximal 96 / Ausgabe TimeService-Limit; `hp_format_time(hp_time_epoch(),"%H:%M")` | keine |

Discovery benutzt den Codec des vorhandenen MinimalMdns, keinen zweiten
Responder/Stack. Temporärer Worker-Socket; maximal 24 DNS-Records/128 Pakete,
1200 ms Suchfenster. IPv4-RFC1918-Resultate, kein Vendor-HTTP-Probing in Lua.
Spezialisierte bestehende Power/Radio-Discovery wurden nicht komplett ersetzt.

## UI und Navigation

Objektargumente sind Handles aus derselben App. Getter/Setter liefern nil bzw.
keinen Wert bei unpassendem Widget; fremde/ungültige Handles werden abgewiesen.
Callback-Argumente sind Namen globaler Lua-Funktionen, keine Closures.
Beispiel: `hp_on_click(button,"pressed"); function pressed() hp_toast("OK") end`.

| Signatur | Rückgabe / Beispiel (keine Permission) |
|---|---|
| `hp_root()` | App-Root; `local root=hp_root()` |
| `hp_label(parent,text)` | Label; `local title=hp_label(root,"Hallo")` |
| `hp_button(parent,text)` | Button mit Label; `local b=hp_button(root,"OK")` |
| `hp_switch(parent,checked)` | Switch; `local sw=hp_switch(root,true)` |
| `hp_panel(parent)` | Transparenter Container; `local p=hp_panel(root)` |
| `hp_slider(parent,min,max,value)` | Slider; `local v=hp_slider(root,0,100,50)` |
| `hp_slider_value(obj)` | Integer; `local v=hp_slider_value(slider)` |
| `hp_set_slider_value(obj,value)` | Kein Wert, kein Value-Event; `hp_set_slider_value(slider,40)` |
| `hp_textarea(parent,text,password,maxLength=96,inputType="text")` | Einzeilige Textarea, maxLength 1–2048; Typ: `text`, `integer`, `decimal`/`number`, `signed_decimal`, `phone`, `datetime`. Numerische Typen öffnen direkt die gemeinsame Zahlentastatur. `local t=hp_textarea(root,"200",false,6,"decimal")` |
| `hp_set_input_type(obj,type)` / `hp_ui_input_type(obj,type)` | Eingabetyp nachträglich setzen; beim nächsten `hp_ui_keyboard_bind` aktiv. Bestehende Apps behalten ohne Typangabe ihre Texttastatur. Dezimalpunkt statt Komma, passend zu `tonumber`. |
| `hp_textarea_text(obj)` | String; `local text=hp_textarea_text(t)` |
| `hp_set_textarea_text(obj,text)` | Kein Wert; `hp_set_textarea_text(t,"device")` |
| `hp_set_password_mode(obj,bool)` | Kein Wert; `hp_set_password_mode(t,true)` |
| `hp_checked(obj)` | Boolean; `local yes=hp_checked(sw)` |
| `hp_align(obj,position,x=0,y=0)` | top_left/top_mid/top_right/center/left_mid/right_mid/bottom_left/bottom_mid/bottom_right; `hp_align(b,"center")` |
| `hp_set_size(obj,w,h)` | Kein Wert; `hp_set_size(b,220,58)` |
| `hp_set_text(label,text)` | Nur Label, identische Texte werden nicht neu gesetzt; `hp_set_text(title,"Neu")` |
| `hp_set_bg_color(obj,rgb)` | RGB-Integer, deckend; `hp_set_bg_color(p,0x172A42)` |
| `hp_set_text_color(obj,rgb)` | Auch direkte Button-Labels; `hp_set_text_color(b,0xffffff)` |
| `hp_set_radius(obj,radius)` | 0–255; `hp_set_radius(p,12)` |
| `hp_set_border(obj,width,rgb,opacity=255)` | Kein Wert; `hp_set_border(p,1,0x7cc4ff,255)` |
| `hp_set_font_size(obj,px)` | Vorhandene Fonts 14/18/24/28/32/48; `hp_set_font_size(title,24)` |
| `hp_set_hidden(obj,bool)` | Kein Wert; `hp_set_hidden(p,true)` |
| `hp_set_enabled(obj,bool)` | Kein Wert; `hp_set_enabled(b,false)` |
| `hp_set_scrollable(obj,bool)` | Vertikales Scrollen; `hp_set_scrollable(p,true)` |
| `hp_set_padding(obj,px)` | Kein Wert; `hp_set_padding(p,8)` |
| `hp_on_click(obj,functionName)`, `hp_on_value_changed(obj,functionName)`, `hp_on_released(obj,functionName)` | Bindet separaten Callback; `hp_on_released(slider,"apply_volume")` |
| `hp_millis()` | Uptime; `local start=hp_millis()` |
| `hp_open_app_settings()` | Boolean: Queue akzeptiert eigene Settings-Navigation |
| `hp_open_app()` | Boolean: Queue akzeptiert eigene Haupt-App, auch aus settings.lua |

### Zusätzliche Widgets / Layout / Theme

| Signatur | Rückgabe / Beispiel (keine Permission) |
|---|---|
| `hp_ui_image(parent,{width,height,pixels})` | Native Image, RGB565-Bytes, maximal 128×128; `hp_ui_image(root,{width=1,height=1,pixels="\0\0"})` |
| `hp_ui_icon(parent,name)` | Symbol-Label aus IconCatalog; `hp_ui_icon(root,"solar")` |
| `hp_ui_spinner(parent)` | Nativer Spinner; `local busy=hp_ui_spinner(root)` |
| `hp_ui_dropdown(parent,options)` | Optionsstring mit newline; `local d=hp_ui_dropdown(root,"A\nB")` |
| `hp_ui_dropdown_value(obj)` | 0-basierter Index; `local selected=hp_ui_dropdown_value(d)` |
| `hp_ui_dropdown_set_value(obj,index)` | Kein Wert; `hp_ui_dropdown_set_value(d,1)` |
| `hp_ui_checkbox(parent,text,checked)` | Checkbox; `local c=hp_ui_checkbox(root,"Aktiv",true)` |
| `hp_ui_checkbox_checked(obj)` | Boolean; `local checked=hp_ui_checkbox_checked(c)` |
| `hp_ui_arc(parent)` / `hp_ui_arc_set(obj,value)` | Arc / Wert 0–100; `hp_ui_arc_set(hp_ui_arc(root),50)` |
| `hp_ui_progress(parent)` / `hp_ui_progress_set(obj,value)` | Bar / Wert 0–100 ohne Animation; `hp_ui_progress_set(bar,50)` |
| `hp_ui_list(parent)` / `hp_ui_list_item(list,text)` | Liste / Button; `hp_ui_list_item(hp_ui_list(root),"Eintrag")` |
| `hp_ui_tabview(parent)` / `hp_ui_tab(tabview,text)` | Tabs / Tab-Container; `local page=hp_ui_tab(tabs,"Live")` |
| `hp_ui_table(parent,rows=1,cols=1)` | Maximal 64×8; `local t=hp_ui_table(root,3,2)` |
| `hp_ui_table_set(table,row,col,text)` | 0-basierte Zelle, maximal 512 Zeichen; `hp_ui_table_set(t,0,0,"W")` |
| `hp_ui_line(parent,points)` | 2–64 `{x,y}`, Koordinaten ±4096; `hp_ui_line(root,{{0,0},{100,50}})` |
| `hp_ui_flex(parent,direction)` | row/row_wrap/column; `hp_ui_flex(p,"column")` |
| `hp_ui_gap(parent,px)` / `hp_ui_padding(obj,px)` / `hp_ui_margin(obj,px)` | 0–128; `hp_ui_gap(p,8)` |
| `hp_ui_grow(obj,value)` | 0–16; `hp_ui_grow(p,1)` |
| `hp_theme()` | background/surface/card/text/text_secondary/accent/success/warning/error RGB; `local t=hp_theme()` |
| `hp_ui_style_card(obj)`, `hp_ui_style_primary(obj)`, `hp_ui_style_input(obj)` | Lokaler UiTheme-Style; `hp_ui_style_card(p)` |
| `hp_toast(text)` | Nicht blockierend, maximal 512, 2.5 s; `hp_toast("Gespeichert")` |
| `hp_alert(title,text)` | Nicht blockierender Dialog mit OK, 128/2048 Zeichen; `hp_alert("Info","Fertig")` |

Icons: energy, solar, battery, grid, house, car, heatpump, wifi, ethernet,
settings, warning, info, audio, play, pause, next, previous, refresh. Bestehende
App-Icon-Namen bleiben unterstützt. Einige semantische Icons teilen dasselbe
vorhandene LVGL-Symbol; keine zusätzlichen Bild-/Font-Pakete.

### Charts

| Signatur | Rückgabe / Beispiel |
|---|---|
| `hp_chart_create(parent,options)` | Chart; type=line/bar, points=1–256, optional min/max zusammen; `local c=hp_chart_create(root,{type="line",points=3})` |
| `hp_chart_add_series(chart,name,rgb?)` | Series-Handle, maximal vier; `local grid=hp_chart_add_series(c,"Netz")` |
| `hp_chart_set_series(chart,series,values)` | Exakt points endliche Werte ±1e9; `hp_chart_set_series(c,grid,{-120,0,350})` |
| `hp_chart_set_labels(chart,labels)` | 2–32 gleichmäßig verteilte X-Labels, je 64 Zeichen; `hp_chart_set_labels(c,{"10:00","10:30","11:00"})` |

Negative Werte bleiben erhalten. Native Legende, automatischer Bereich über
alle Serien oder manuelles min/max. Vorhandene Punkte werden nur bei Änderung
überschrieben; Widget und Serien werden nicht pro Update neu erzeugt.

### Beibehaltene, deprecated UI-Namen

Diese Namen verweisen weiterhin auf gemeinsame UI-Implementierungen, nicht
auf Power-spezifische C++-Duplikate. Für neue Apps obige klare APIs benutzen.

| Signatur | Vertrag / Beispiel |
|---|---|
| `hp_ui_container(parent)` / `hp_ui_panel(parent)` | Transparent / Glass-Container; `local p=hp_ui_panel(root)` |
| `hp_ui_hidden(obj,bool)` | Wie hp_set_hidden; `hp_ui_hidden(p,true)` |
| `hp_ui_text_style(obj,size,tone)` | tone primary/accent/positive/warning/secondary; `hp_ui_text_style(title,24,"primary")` |
| `hp_ui_button_primary(obj,bool)` | Primary-/Card-Style; `hp_ui_button_primary(b,true)` |
| `hp_ui_button_text(obj,text)` | Button-Label mit Dirty-Check; `hp_ui_button_text(b,"Play")` |
| `hp_ui_textarea(parent,text,password)` / `hp_ui_textarea_get(obj)` / `hp_ui_textarea_set(obj,text)` | Wie hp_textarea_*; `local text=hp_ui_textarea_get(t)` |
| `hp_ui_keyboard(parent)` / `hp_ui_keyboard_bind(keyboard,textarea)` / `hp_ui_keyboard_hide(keyboard)` | Native Tastatur; `hp_ui_keyboard_bind(k,t)` |
| `hp_ui_chart(parent)` / `hp_ui_chart_set(chart,arrayA,arrayB)` | Alter Zwei-Bar-Chart, maximal 256; `hp_ui_chart_set(c,{1,2},{3,4})`; negative Werte hier legacy-clamped, neue API verwenden |

## Media, Radio und Energie

| Signatur | Rückgabe / Beispiel | Permission |
|---|---|---|
| `hp_now_playing()` | title/subtitle/playing/volume, nonblocking Snapshot; `local s=hp_now_playing()` | media |
| `hp_media_command(name)` | Job-ID/nil, play_pause/stop/next/previous/power_toggle/mute_toggle/volume_up/volume_down; `hp_media_command("next")` | media |
| `hp_music_snapshot()` | title/count/volume/error; `local s=hp_music_snapshot()` | media |
| `hp_music_play_index(index)` | 0-basierter Index, vorhandener Dienst; `hp_music_play_index(0)` | media |
| `hp_energy_snapshot()` | nil bei Lock-Contention, sonst Snapshot mit valid/online/status/source/timestamp/revision, Zahlen und quality; `local s=hp_energy_snapshot()` | energy |
| `hp_energy_history(period)` | Job-ID/nil, live/hour/today/week/month/year; JSON-Array in job_result.body; `hp_energy_history("today")` | energy |

EnergySnapshot: gridPower, housePower, pvPower, batteryPower, batterySoc,
gridImportTotal/gridExportTotal (kWh), pvEnergyToday, houseEnergyToday,
batteryChargeToday/batteryDischargeToday; phase[1..3] voltage/current/power.
Nicht vorhandene Werte sind nil/quality=unavailable, niemals erfundene Nullen.
Quality measured/calculated/unavailable, Native-Werte behalten Source. Der
bestehende IR-Tracker/Shelly-Pfad liefert derzeit Netz/Phasen/Zähler; PV/Haus/
Batterie brauchen zusätzliche reale Provider. Provider maximal acht, cached
snapshots ohne I/O; measured vor calculated, dann höhere Provider-Priorität.
Stale nach 30 s: valid/online=false. Neue Hersteller-Stubs wurden nicht angelegt.

History wird im bestehenden Worker gepflegt: Minuten-Samples, Stunden/Tage/
Kalendermonate, signed powers, verfügbare kumulative Import/Export-Zähler.
Kein erfundenes Energieintegral. SD-Profil 1440/168/366/24 Punkte, interner
Fallback 60/48/31/12, PSRAM-Rings. Auf SD Jahresabdeckung; intern bewusst kürzer.
Persistenz höchstens alle 30 min, atomarer Ersatz mit CRC; Stromausfall kann die
letzten 30 min Samples verlieren. Lua-Ergebnis momentan maximal 64 Punkte;
UTC-Stunden, lokale Tagesgrenzen. Noch keine historische Detail-Quality/Source
pro Sample oder automatisch integrierte Tages-PV-/Batterieenergie.

### Radio-Domain-API (media)

Diese APIs bleiben ausschließlich in RadioLuaBridge/RadioService. Keine
app-spezifischen C++-Navigations-/Enable-Bridges. Shared Radio ist ein
privilegierter Media-Dienst; generische HTTP/Discovery sind separat gated.

| Signatur | Rückgabe / Beispiel |
|---|---|
| `hp_radio_snapshot()` | Gecachter Radio-State, valid=false bei Contentention; `local s=hp_radio_snapshot()` |
| `hp_radio_command(name)` | Boolean,message; `hp_radio_command("previous")` |
| `hp_radio_set_volume(percent)` | Boolean,message; `hp_radio_set_volume(30)` |
| `hp_radio_request_modes()` / `hp_radio_modes()` | Queue-Boolean / Liste key,label; `hp_radio_request_modes()` |
| `hp_radio_select_mode(key)` | Queue-Boolean,message; `hp_radio_select_mode(3)` |
| `hp_radio_request_presets()` / `hp_radio_presets()` | Queue / Liste; `local rows=hp_radio_presets()` |
| `hp_radio_select_preset(key)` | Queue; `hp_radio_select_preset(1)` |
| `hp_radio_browse_root()` / `hp_radio_browse_back()` | Queue; `hp_radio_browse_root()` |
| `hp_radio_browse_items()` / `hp_radio_browse_select(index)` | Liste / Queue, 0-basierter Index; `hp_radio_browse_select(0)` |
| `hp_radio_catalog_busy()` / `hp_radio_catalog_revision()` | Boolean / Revision; `local revision=hp_radio_catalog_revision()` |
| `hp_radio_discover()` / `hp_radio_discovery_status()` / `hp_radio_discovered()` | Queue / Text / bis acht native Resultate; `hp_radio_discover()` |
| `hp_radio_use_discovered(index)` | 0-basierter Index, queued Speicherung; `hp_radio_use_discovered(0)` |
| `hp_radio_settings()` | Native Optionen und pin_present, **kein PIN**; `local s=hp_radio_settings()` |
| `hp_radio_save_settings(host,pin,parental,limit,maxPercent,lockSources,lockPower,lockDisplay)` | accepted,message; Persistenz später im Worker; `hp_radio_save_settings("192.168.1.20","",false,false,60,false,false,false)` |

`hp_power_*` bleiben als altes Domain-Modul im Quellcode, sind in neuen States
nicht registriert. Stattdessen hp_energy_*, generische Discovery/Storage/Charts.

## Bewusst noch nicht angeboten

MQTT, WebSocket und HomeAssistant besitzen im bestehenden Projekt keinen
fertigen gemeinsamen Client-/Subscription-/Reconnect-Dienst. Ihre Permissions
sind reserviert, aber es gibt keine hp_mqtt_*, hp_ws_* oder hp_ha_*-Fake-APIs.
Folgearbeit: native bounded multiplexed service, app-isolierte Subscriptions,
Credentials ausschließlich nativ, queued Messages/Disconnect/Cleanup, echte
Transport-/Reconnect-Tests. HTTP/JSON/Jobs/Events bilden die gemeinsame Basis.
Keine Hersteller-/Modbus-/MQTT-EnergyProvider-Platzhalter mit erfundenen Daten.

Hardwaretests (Touch/Display/DSI, echtes LAN/TLS/SD-Stromausfall) sind durch
Host-Tests/Build nicht ersetzt. Display-/Rotation-/C6-Treiber wurden nicht geändert.

## Build-/Speichervergleich

Legacy-P4/360-MHz-Build vor dem Umbau: 64536 Byte statisches RAM,
3021918 Byte Flash. Nach dem Plattformumbau: ungefähr 67300 Byte RAM und
3.14 MB Flash, also rund +2.7 KiB RAM / +116 KiB Flash. Keine neuen dauerhaften
Tasks; dieselben BSP-/NetworkWorker-/BackgroundScheduler-Tasks bleiben Besitzer.
Das sind Linkerwerte, keine Messung der laufenden Hardware-Heap-/PSRAM-Belegung.

Dynamisch: Lua bis 1 MiB pro State, Validator zusätzlich temporär bis 1 MiB;
Grow-Realloc kann vorübergehend alten und neuen Block gleichzeitig halten.
JSON-Allocator bis 64 KiB PSRAM, vier Responses bis je 16 KiB, Bilddatenbudget
512 KiB, DNS-Record-Cache ungefähr 10 KiB PSRAM. EnergyHistory-SD-Rings
ungefähr 96 KiB, kleiner Fallback ungefähr 7 KiB; atomare Persistenz braucht
vorübergehend einen zusätzlichen gleich großen PSRAM-Buffer. LVGL-Widget-
Speicher ist zusätzlich durch das Konstruktionsbudget begrenzt.

Validiert: 97 Python-/Host-Tests plus Architekturchecks, release_validator
source-only, Legacy-Firmware-Build. Neue Host-Tests kompilieren die echte Lua-
Sandbox, Allocator-/Callback-Budgets, ArduinoJson-Bridge, Job-Slot-Verwaltung,
Pfad-/Netzvalidatoren und Energy-Value-Priorisierung. Netzabschluss ist im
Job-Test ein explizites Testdouble, kein behaupteter TCP-/TLS-Hardwaretest.
Bestehende Compilerwarnungen aus SettingsApp und gepinnten Vendor-Komponenten
wurden nicht durch sachfremde Änderungen beseitigt. Die unbenutzte native
Legacy-StromApp wurde später entfernt; der gemeinsame Energie-Dienst bleibt.
