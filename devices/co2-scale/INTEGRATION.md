# Eigenständige CO₂-Waage

Unveränderte lokale ESP32/HX711-Firmware vom 22.10.2026, vom P4-Projekt getrennt.
`platformio.ini`, Weboberflächen, Lizenztext und `device_config.h` gehören zu
diesem Gerät. Keine dieser Dateien wird in die Home-Display-Firmware eingebunden.

In diesem Verzeichnis mit `pio run` bauen. Das konfigurierte Gerät ist ein
`esp32dev` an COM5, **nicht** der ESP32-P4 an COM7. Vor einem Flashvorgang Board,
Verdrahtung, Port und Partitionen am tatsächlichen Waagengerät prüfen.

Die Waage benötigt HX711-Hardware. Sie ist keine Lua-App und läuft nicht auf dem
Display. Ihre bestehende Weboberfläche und Daten bleiben unverändert.
