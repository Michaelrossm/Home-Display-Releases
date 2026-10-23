# Separate Web-Einstellungen für Apps

## Ziel

Display- und Browser-Einstellungen sind zwei verschiedene Oberflächen, benutzen
aber dieselben gespeicherten App-Werte.

Ein Lua-App-Paket kann künftig so aufgebaut sein:

```text
app.json
main.lua
settings.lua          # optional: LVGL-Seite auf dem Display
web_settings.json     # optional: Web-Seite auf Port 80
assets/
```

`web_settings.json` enthält **keinen ausführbaren Code**. Die Firmware rendert
daraus selbst HTML. Dadurch bleibt der Port-80-Webserver zentral, sicher und
ressourcensparend.

Bestehende Apps ohne `web_settings.json` funktionieren unverändert.

## Schema 1

```json
{
  "schema": 1,
  "tabs": [
    {
      "id": "general",
      "label": "Allgemein",
      "fields": [
        {
          "key": "refresh_minutes",
          "label": "Aktualisierung",
          "type": "integer",
          "default": 10,
          "min": 5,
          "max": 120,
          "step": 5
        },
        {
          "key": "enabled",
          "label": "Aktiv",
          "type": "bool",
          "default": true
        }
      ]
    },
    {
      "id": "network",
      "label": "Netzwerk",
      "fields": [
        {
          "key": "host",
          "label": "IP / Hostname",
          "type": "host",
          "maxLength": 96
        },
        {
          "key": "password",
          "label": "Passwort",
          "type": "password",
          "maxLength": 256
        }
      ]
    }
  ]
}
```

Unterstützte einfache Typen:

- `text`
- `host`
- `url`
- `password`
- `bool`
- `integer`
- `number`
- `select`

Optionale Attribute:

- `default`
- `help`
- `placeholder`
- `min`
- `max`
- `step`
- `maxLength`
- `options` bei `select`

Secrets werden nie ausgelesen. Ein leeres Passwortfeld behält das vorhandene
Secret. Der Browser kann ein vorhandenes Secret ausdrücklich löschen.

## Wiederholbare Gruppen

Für z. B. mehrere Zähler, Tracker, Räume oder Geräte gibt es `repeater`.

```json
{
  "key": "sources_json",
  "label": "Zähler und Tracker",
  "type": "repeater",
  "itemLabel": "Zähler",
  "maxItems": 8,
  "fields": [
    {"key":"enabled","label":"Aktiv","type":"bool"},
    {"key":"name","label":"Name","type":"text","maxLength":48},
    {"key":"host","label":"IP / Host","type":"host","maxLength":96},
    {
      "key":"role",
      "label":"Rolle",
      "type":"select",
      "options":[
        {"value":"auto","label":"Automatisch"},
        {"value":"grid","label":"Netz"},
        {"value":"pv","label":"Photovoltaik"},
        {"value":"battery","label":"Speicher"}
      ]
    }
  ]
}
```

Der Repeater liest und schreibt einen JSON-Array-String im normalen KV-Schlüssel.
Passwörter sind innerhalb eines Repeaters absichtlich nicht erlaubt, damit
Secrets nicht versehentlich im Klartext in JSON landen. Solche Secrets werden
als separate `password`-Felder im normalen Secret-Namespace beschrieben.

## Kompatibilitätsmodus

Apps ohne `web_settings.json`:

- einfache bestehende KV-Werte werden weiterhin automatisch angezeigt
- technische Schlüssel werden lesbarer dargestellt
- Secrets werden nur als gesetzt/nicht gesetzt angezeigt
- strukturierte JSON-Werte wie `sources_json` werden **nicht mehr als rohe
  Textbox angeboten**
- bei solchen komplexen Einstellungen zeigt die Weboberfläche an, dass eine
  `web_settings.json` benötigt wird
- die bestehende Display-Einstellungsseite kann weiterhin direkt geöffnet werden

Damit kann eine alte App nicht versehentlich durch das Bearbeiten eines
komplexen internen JSON-Strings beschädigt werden.

## Wichtige Regel

`settings.lua` und `web_settings.json` müssen dieselben KV-/Secret-Schlüssel
verwenden. Es gibt keine zweite Einstellungsdatenbank.
