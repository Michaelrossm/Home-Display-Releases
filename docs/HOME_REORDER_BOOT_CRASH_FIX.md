# Homescreen-Reorder Boot-Crash-Fix

Der erste Long-Press-Reorder-Patch verwendete mehrere große lokale Arrays aus
`AppRegistryEntry` und `String`.

Betroffene Pfade:

- `AppRegistry::rebuild()` -> 48 komplette Registry-Einträge
- `HomeAppOrderService::sortEntries()` -> weitere 48 Registry-Einträge + 48 Strings
- `HomeReorderController::bindGrid()` -> nochmals 48 Registry-Einträge
- `HomeAppOrderService::move()` -> 48 Registry-Einträge + 48 Strings

Da `AppRegistryEntry` mehrere Arduino-Strings enthält, konnte der UI-/Main-
Task-Stack bereits beim Aufbau des Homescreens überlaufen. Das äußert sich als
türkiser Startbildschirm, Neustartschleife und schließlich Recovery.

Fix:

- Registry-Rebuild schreibt direkt in den persistenten Registry-Speicher
- Sortierung erfolgt in-place mit nur einem temporären `AppRegistryEntry`
- Launcher-Bindung liest Registry-Einträge einzeln über `copyAt()`
- Persistierte Reihenfolge wird als kompakter `String` bearbeitet
- kein 48er-AppRegistry-Array mehr im Reorder-Pfad
- alte Touch-Overlays werden nach einem Reorder entfernt, bevor neu gebunden wird

App-Löschen bleibt weiterhin ausschließlich in Einstellungen/Web.
