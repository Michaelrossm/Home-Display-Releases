# Home-Display — Entwicklungsübergabe

Verbindliche Regeln: [AGENTS.md](AGENTS.md). Einstieg und Dokumentationsindex:
[README.md](README.md), [docs/README.md](docs/README.md).

## Arbeitsablauf

```powershell
python tools/dev.py doctor
python tools/dev.py test
python tools/dev.py validate
python tools/dev.py build --env home_display_p4_legacy_dev
```

Standard aktuell Legacy / 360 MHz. R3 / 400 MHz nur ausdrücklich für bestätigtes
v3.x-Silizium. Keine Firmware bei unbekannter Revision flashen.
C6-Binärdateien bleiben separat unter `firmware/c6/`.

## Wichtige Grenzen

Bestehende lokale Änderungen erhalten. Keine alten Patch-Installer oder Generatoren
auf den aktuellen Baum anwenden. Allgemeine Funktionen gehören in die vorhandenen
Dienste/Bridges, nicht in app-spezifische Kopien. Keine zusätzlichen permanenten
Tasks, keine zweite LVGL-/mDNS-/Storage-Verwaltung.

`src/CMakeLists.txt` wird auch im ESP-IDF-Scriptmodus ausgewertet: plain
`GLOB_RECURSE`, niemals `CONFIGURE_DEPENDS`.
Der Waveshare-BSP besitzt LVGL; UI-Mutationen benötigen den vorhandenen UI-Lock.
Gepinnte Stackversionen stehen in AGENTS.md und platformio.ini.

## Repository-Grenzen

`apps/`, installierbare App-ZIPs und `dist/*.zip` sind lokale/externe Artefakte.
Native integrierte Apps bleiben unter `src/apps/`. Private Schlüssel und
Zugangsdaten niemals committen. Build-Caches und managed_components nicht hochladen.
Aktueller Prüfstand: [docs/VALIDATION.md](docs/VALIDATION.md).

Nicht automatisch flashen oder veröffentlichen; dafür ist der jeweilige
Benutzerauftrag maßgeblich.
