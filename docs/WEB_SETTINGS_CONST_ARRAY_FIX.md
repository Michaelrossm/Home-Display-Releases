# web_settings.json Const-Array-Fix

Die strenge Schema-Prüfung iteriert Tabs/Felder als `JsonObjectConst`.
Container innerhalb dieser Objekte müssen deshalb mit den passenden
ArduinoJson-Const-Typen geprüft werden.

Korrigiert:

- `JsonArray` -> `JsonArrayConst` für `tabs`
- `JsonArray` -> `JsonArrayConst` für Tab-`fields`
- `JsonArray` -> `JsonArrayConst` für Repeater-`fields`
- `JsonArray` -> `JsonArrayConst` für Select-`options`

Ohne diesen Fix konnte eine gültige Datei fälschlich mit
`web_settings_tab_shape` abgelehnt werden.

Außerdem unterscheidet die Weboberfläche bei Installationsfehlern jetzt zwischen
einem fehlgeschlagenen Update und einer fehlgeschlagenen Neuinstallation.
