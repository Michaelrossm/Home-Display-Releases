# Home-App-Reihenfolge per Long-Press

Auf dem Homescreen können App-Kacheln jetzt per Long-Press und Drag & Drop
verschoben werden.

## Bedienung

- kurzer Tap: App öffnen
- lange auf eine App drücken: Drag-Modus
- Finger auf eine andere Position bewegen
- loslassen: App wird an diese Position verschoben
- Reihenfolge wird dauerhaft in NVS gespeichert

## Sicherheit / Verhalten

Es gibt auf dem Homescreen bewusst **keinen Löschen-Button und kein X**.

Apps können weiterhin ausschließlich über:

- Einstellungen
- Weboberfläche

gelöscht werden.

## Architektur

`HomeAppOrderService` speichert nur stabile App-IDs in `ui_app_order`.

`AppRegistry::rebuild()` sortiert user-installierbare Apps nach dieser
gespeicherten Reihenfolge. Dadurch gilt die Reihenfolge auch nach Neustart,
App-Scan und Reinstall.

`HomeReorderController` legt transparente Touch-Overlays über die Launcher-
Kacheln. Dadurch werden normale Taps und Long-Press-Drag sauber getrennt,
ohne die bestehenden App-Callback-Implementierungen zu verändern.

NVS wird nicht im LVGL-Touch-Callback beschrieben. Der Drag setzt nur einen
kleinen Pending-Move; die Persistierung erfolgt anschließend im Hauptloop.

Der feste Dock (Radio/Musik/Wetter/Einstellungen) bleibt unverändert.
