# Gemeinsame Druckeraktionen

Die bestehenden PrinterService-/LuaJobService-/NetworkWorker-Pfade bleiben
Eigentuemer aller Netzwerkoperationen. Keine weiteren Tasks, Webserver,
automatischen Schreib-Retries oder Hintergrund-Steuerbefehle.

`hp_printer_command(profile, action, parameters)` verarbeitet die neuen Aktionen
asynchron. Snapshot/summary enthalten `controls` mit booleschen Freigaben,
`homed`, `channel_idle`, `extrusion_ready` und `calibration_homes`.
`hp_printer_capabilities()` meldet `typed_controls` und `profile_controls`.
App-Manifest bleibt Schema 3, web_settings.json Schema 1, Printer-Schema 2.

| Aktion | RepRapFirmware | Klipper/Moonraker | Marlin/OctoPrint |
| --- | --- | --- | --- |
| home | G28 mit ausgewaehlten Achsen | G28 | OctoPrint home API |
| bed_mesh | G29 S0, Probe und M557-Raster erkannt | BED_MESH_CALIBRATE, bed_mesh konfiguriert | Nur Profilfreigabe: G28 + G29 oder G29 P1 |
| z_tilt | G32, bed.g im Profil ausdruecklich freigegeben | Z_TILT_ADJUST, z_tilt konfiguriert | Nur Profilfreigabe: G28 + G34 |
| gantry_level | keine pauschale Gleichsetzung | QUAD_GANTRY_LEVEL, Modul konfiguriert | gesperrt |
| screws_tilt | gesperrt | SCREWS_TILT_CALCULATE, Modul konfiguriert | gesperrt |
| mesh_clear | G29 S2 | BED_MESH_CLEAR | gesperrt |
| select_tool | T mit erkannter Werkzeugnummer | ACTIVATE_EXTRUDER mit erkanntem Namen | OctoPrint select API mit erkanntem Tool |
| extrude | relative Extrusion, M120/M121 | relative Extrusion, SAVE/RESTORE_GCODE_STATE ohne XYZ-Rueckfahrt | gesperrt: keine verifizierte thermische Freigabe |
| set_flow_factor | M221 D mit aktivem, eindeutigem Extruder | M221 fuer den aktiven Extruder | gesperrt |
| motors_off | M18 | M18 | gesperrt |

Kalibrierung/Werkzeugwechsel erfordern bei RepRap/Klipper Leerlauf, freien
Steuerkanal und bekannte referenzierte XYZ-Achsen. Bei OctoPrint ist der
Referenzierungszustand unbekannt; die manuell freigegebene Kalibrierung enthaelt
deshalb G28 und laeuft nur bei aktuell geprueftem operational/ready/idle.
Ein Toolwechsel kann druckerseitige Makros ausfuehren; insbesondere Klippers
ACTIVATE_EXTRUDER ersetzt kein beliebiges T0/T1-Park-/IDEX-Makro.

Extrusion: ausschliesslich aktives, sicher bekanntes Werkzeug, heiße Duese,
Leerlauf und freier Kanal; maximal +/-10 mm, 1..300 mm/min. Die App verwendet
60 mm/min. Keine automatische Heizung oder Werkzeugwahl vor Extrusion.
Materialfluss ist auf 10..200 % begrenzt; Anpassung im Druck bleibt moeglich.

Profilfelder (optional, Standard none): `calibration` = none, marlin_bilinear,
marlin_ubl; `alignment` = none, marlin_g34, rrf_bed_g. Native Einstellungen und
Webformular verwenden dieselben KV-Felder. Aenderungen invalidieren die
Befehlsidentitaet, damit wartende Auftraege nicht mit geaenderter Freigabe laufen.
Die Firmware kann Marlins eingebautes Leveling-Verfahren nicht aus den normalen
OctoPrint-Endpunkten ermitteln. Keine automatische Freigabe aufgrund des Namens.

Kein automatisches M500, SAVE_CONFIG, Neustarten, PID-Tuning, Z-Offset-Schreiben,
Filament-Ladeprogramm oder Emergency-Stop-Versprechen ueber eine normale Queue.
Motoren loesen kann Z absinken lassen. App-Aktionen sind bestaetigungspflichtig.
Job-Erfolg bedeutet nur Annahme, nicht fertig ausgefuehrte Bewegung/Kalibrierung.
Nach Timeout wird ein unklarer Schreibbefehl nie automatisch wiederholt.

Tests: test_printer_controls.py (reale Adapter mit simuliertem Transport),
test_printer_app_runtime.py (pinned Lua), test_printer_web_fix.py und vorhandene
Queue-/Profil-/Safety-Tests. Keine physische Steuerungsvalidierung dadurch.

Referenzen:
- https://www.klipper3d.org/G-Codes.html
- https://docs.duet3d.com/User_manual/Reference/Gcodes
- https://marlinfw.org/docs/gcode/G029.html
- https://marlinfw.org/docs/gcode/G034-zsaa.html
- https://docs.octoprint.org/en/main/api/printer.html
