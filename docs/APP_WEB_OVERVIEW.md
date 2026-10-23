# Gemeinsame App-Weboberflächen

Die Browser-Übersicht verwendet `AppRegistryEntry::webPath`. Native Apps
deklarieren ihre vorhandene Route über `IApp::webPath()`. Lua-Pakete mit
`web_settings.json` erhalten `/apps/<eigene-ID>/` auf dem bestehenden Port 80.
Beispiele sind `/apps/printer_panel/`, `/apps/strom/` und `/apps/radio/`, jedoch
nur wenn das jeweilige installierte Paket eine Weboberfläche deklariert.
Es werden keine Platzhalter-Buttons für nicht installierte Apps angelegt.
Alte Links mit `/?app_settings=<eigene-ID>` bleiben kompatibel. Das Schema bleibt
Version 1; KV-Daten, Berechtigungen, Anmeldung und CSRF bleiben zentral.
Es entstehen keine zusätzlichen HTTP-Server oder Tasks. Beliebige eigene
HTML-/Lua-HTTP-Handler sind damit nicht freigeschaltet.

Die neue App-Adresse liefert die vorhandene generische Web-Einstellungsoberfläche,
keine automatisch erzeugte Drucker-/Radio-Steuerkonsole. Die App stellt ihre
Felder in `web_settings.json` bereit; KV-Schlüssel entsprechen denen am Display.
Direkte Aufrufe unbekannter Apps oder Apps ohne Web-Deskriptor liefern 404.
IDs, zusätzliche Pfadsegmente und kodierte Pfadseparatoren werden strikt geprüft.
Anmeldung, CSP und CSRF bleiben erhalten. Der direkte App-Aufruf lädt keine
System-/Diagnosedaten und keine gesamte App-Liste. Native Weboberflächen und
externe Web-Verknüpfungen behalten ihre bereits registrierten Adressen.

`hp_web_settings_capabilities()` meldet zusätzlich `on_demand=true` und
`background_tasks=false`. App-Webseiten verwenden das gemeinsame geschützte
`/app-web-lifecycle.js`: GET-Abfragen werden beim Verstecken/Schließen
abgebrochen, Folgeabfragen und neue Aufträge pausieren. Eine versteckt
geladene Seite wartet mit ihrem ersten Datenabruf bis zur Sichtbarkeit.
Schreibaufträge werden nach Übergabe nicht automatisch wiederholt oder
als serverseitig abgebrochen ausgegeben. Bereits gestartete Worker-Aufträge
dürfen begrenzt fertig werden; unbeobachtete Ergebnisse unterliegen der
bestehenden LuaJobService-Aufbewahrungsgrenze. Keine per-App-Timer oder
zusätzlichen Firmware-Tasks. Kalender-Sync und Klingelüberwachung sind
eigene zentrale Dienste, unabhängig von der Weboberfläche, und bleiben aktiv.

Die Paket-Erkennung speichert den Deskriptor beim Scannen bzw. Installieren.
HTTP-Handler lesen nur kopierte Registry-Daten, ohne Dateisystem-Erkennung
oder Dereferenzierung lebender App-Objekte. Der größere Registry-Snapshot
wird geprüft auf dem Heap (bevorzugt PSRAM) statt auf dem HTTP-Stack angelegt.

Die Übersicht zeigt auch deaktivierte Apps mit Weboberfläche. Das ermöglicht
Konfiguration ohne Start der Display-App. Externe Web-Apps bleiben reine
Links und werden nicht automatisch kontaktiert. Nur HTTP/HTTPS-Links sind
zulässig; Firmware-Routen bleiben auf derselben Origin.

Netzwerk, System und Diagnose befinden sich in einem gemeinsamen
Einstellungsreiter. Daten werden erst beim Öffnen des jeweiligen Bereichs
angefragt; laufende gleiche Bereichs-Anfragen werden zusammengefasst.
Ausstehende WLAN-Aktualisierungen und Service-Polling starten außerhalb
des aktiven Bereichs keine Folgeabfragen. Bereits laufende HTTP-Anfragen
werden nicht als unterbrechbar behandelt; ihre Inhalte bleiben unsichtbar,
wenn der Nutzer den Bereich verlässt.

`tests/test_web_dashboard.py` prüft das echte eingebettete JavaScript mit
Node: Syntax, initiales Ladebudget, Navigation, versteckte Browser-Seite,
Web-Katalog und sichere Links. Geräte-/Browser-Hardwaretest steht aus.
