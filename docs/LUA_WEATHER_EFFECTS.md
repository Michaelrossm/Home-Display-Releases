# Lua-Wettereffekte

Die Wettereffekte laufen nativ im vorhandenen LVGL/UI-Kontext.

Architektur:
- keine neue FreeRTOS-Task
- kein Lua-`on_tick()` fuer Animationen
- kein BackgroundScheduler fuer Animationen
- genau ein gemeinsamer LVGL-Timer fuer alle Wettereffekte
- maximal ein Wettereffekt pro Lua-App
- maximal vier gleichzeitig vorhandene Effekte (Foreground + suspendierte LRU)
- maximal acht Partikel pro Effekt
- Effektflaeche maximal 160 x 120 Pixel
- keine GIFs, PNG-Framefolgen oder per-Frame-Heap-Allokationen
- alle Effektobjekte werden einmal beim Erzeugen erstellt und danach nur verschoben
- beim Loeschen des Effektcontainers wird der Eintrag automatisch entfernt

Leistungsstufen:
- Foreground + Idle Active: ca. 12 FPS (`83 ms`)
- Dimmed: ca. 4,5 FPS (`220 ms`)
- Saver: Timer pausiert
- Display Off: Timer pausiert
- Lua-App Suspended: Timer pausiert, sobald kein Foreground-Effekt mehr vorhanden ist
- BackgroundExecuting: kein Animationslauf
- Evicted/Close: Effekt verschwindet mit dem App-Objektbaum

Der Dienst verwendet `UiTheme::generation()`. Bei einem Theme-Wechsel werden nur die
wenigen Effektfarben neu gesetzt:
- Regen, Nieselregen, Wind: `UiTheme::accent()`
- Sonne / Gewitter: `UiTheme::warning()`
- Schnee / Nebel / Wolken: `UiTheme::textSecondary()`

## Lua-API

```lua
local effect = hp_ui_weather_effect(weather_card, {
    type = "rain",
    intensity = 40,
    width = 140,
    height = 100
})

hp_ui_weather_effect_set(effect, {
    type = "fog",
    intensity = 25
})

hp_ui_weather_effect_set(effect, { enabled = false })
```

Unterstuetzte Typen:
- `clear`
- `partly_cloudy`
- `cloudy`
- `fog`
- `drizzle`
- `rain`
- `heavy_rain`
- `snow`
- `storm`
- `wind`
- `clear_night`

`intensity` ist 0..100. Die Firmware begrenzt daraus selbst die Zahl der sichtbaren
Partikel; Lua erzeugt keine Tropfen/Flocken einzeln.

## Open-Meteo-Zuordnung

Empfohlen:
- 0 -> `clear`
- 1..2 -> `partly_cloudy`
- 3 -> `cloudy`
- 45/48 -> `fog`
- 51..57 -> `drizzle`
- 61..67 -> `rain`
- 71..77 -> `snow`
- 80..82 -> `heavy_rain`
- 85..86 -> `snow`
- 95..99 -> `storm`

Nachts kann Code 0 auf `clear_night` gemappt werden.

## Semantische Wetter-Symbole

`hp_ui_icon(parent, "rain")` und die anderen Wetter-Namen verwenden jetzt bereits
vorhandene Noto-Emoji-Glyphen. Dadurch ist kein groesserer Font noetig.

Verfuegbar sind u. a.:
`clear`, `sun`, `clear_night`, `moon`, `partly_cloudy`, `cloudy`, `drizzle`,
`rain`, `heavy_rain`, `storm`, `thunderstorm`, `snow`, `snowflake`, `wind`,
`temperature`, `umbrella`, `rainbow`.

Alternativ liefert `hp_weather_symbol("rain")` direkt das passende Symbol.

Bei `fog` sollte bevorzugt `hp_ui_weather_effect(..., {type="fog"})` verwendet
werden: drei native Nebelbaender sind deutlich aussagekraeftiger als ein Wolken-Emoji.
