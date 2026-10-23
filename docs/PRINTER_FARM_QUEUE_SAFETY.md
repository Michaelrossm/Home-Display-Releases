# Druckfarm-Paket und zentrale PrinterService-Sicherheit

Das externe Schema-3-Paket `printer_panel` verwendet die bestehende PrinterService-
API v2, getrennte `main.lua`/`settings.lua`-States und `web_settings.json` Schema 1.
Es wird nicht ins Firmware-Repository aufgenommen. Die Web-Konfiguration läuft
auf Port 80 unter `/apps/printer_panel/`, ohne eigenen Server oder Task.

Die App hält bis zu zwölf manuell freizugebende Aufträge je Druckerprofil im
owner-scoped KV-Speicher. Die tatsächliche Grenze von 2 KiB wird beim Hinzufügen
geprüft. Start erst nach bestätigter Speicherung; zweite Bestätigung innerhalb
von fünf Sekunden. Während eines Startbefehls bleibt die Warteschlange unverändert.
Beim Schließen/Resume werden ausstehende Speicheränderungen erneut als ungespeichert
markiert. Eine langsame Speicherung wird nicht parallel erneut eingereiht.

Kein automatischer Start nach Reconnect, App-Wechsel oder Neustart. Ohne verifiziertes
Teileentnahme-Signal ist automatische Fortsetzung ausdrücklich gesperrt. Ein HTTP-
Erfolg bestätigt die Annahme des Befehls, nicht die physische Ausführung des Drucks.

PrinterService lehnt Steuerbefehle nach mehr als zehn Sekunden Queue-Wartezeit ab,
bevor Profile geladen oder Netzwerkbefehle gesendet werden. Die Befehlsreservierung
wird freigegeben; Fehler `printer_command_queue_expired`. Upload und Preview sind
ausgenommen. Bereits laufende synchrone Requests werden dadurch nicht unterbrochen.
Schreibbefehle werden nicht automatisch wiederholt. Fähigkeiten melden
`command_queue_limit_ms=10000` und `automatic_queue_start=false`.

Hardwareprüfung und reale Druckstarts müssen gesondert erfolgen. Es wurden keine
Druckerbefehle gesendet, keine Firmware geflasht und keine Daten migriert.
