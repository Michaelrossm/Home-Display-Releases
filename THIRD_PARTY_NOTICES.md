# Home-Display: Hinweise zu Drittsoftware

Der eigene Home-Display-Projektcode steht unter der GNU General Public License
Version 3.0 (GPL-3.0-only) (siehe LICENSE.md), Copyright 2026 Michael Roßmann.
Diese Lizenz ersetzt oder verändert keine Lizenz eingebundener Fremdsoftware.

Wesentliche Komponenten und ihre Originalquellen:

- Arduino-ESP32: LGPL-2.1-or-later; https://github.com/espressif/arduino-esp32
- ESP-IDF: Apache-2.0 und gesonderte Lizenzen gebündelter Komponenten;
  https://github.com/espressif/esp-idf
- pioarduino / PlatformIO-Plattform: siehe Original-Lizenz;
  https://github.com/pioarduino/platform-espressif32
- Waveshare ESP32-P4 BSP und Espressif-Displayadapter: siehe Komponentenlizenzen;
  https://components.espressif.com/components/waveshare/esp32_p4_platform
- LVGL: MIT; https://github.com/lvgl/lvgl
- Lua: MIT; https://www.lua.org/license.html
- ArduinoJson: MIT; https://github.com/bblanchon/ArduinoJson
- libpng und zlib: jeweils eigene libpng-/zlib-Lizenz;
  https://www.libpng.org/pub/png/libpng.html und https://zlib.net/zlib_license.html
- libwebp: BSD-artige Lizenz und PATENTS-Hinweis;
  https://chromium.googlesource.com/webm/libwebp/
- libsmb2: LGPL-2.1; https://github.com/sahlberg/libsmb2
- esp-audio-player und Helix-MP3-Decoder: siehe jeweilige Originaltexte;
  https://github.com/chmorgan/esp-audio-player
- Schriftdateien und Bild-/Video-Codecs: jeweils eigene Lizenztexte und Hinweise.

Die Release-Quellen und das Downloadpaket enthalten die aus dem verwendeten
Build exportierten Original-Lizenzdateien im Ordner licenses/third-party.
Diese Übersicht ist kein Ersatz für die verbindlichen Originaltexte und keine
rechtliche Zertifizierung. Fremdcode bleibt seinen jeweiligen Urhebern zugeordnet.

Projektlizenz: https://www.gnu.org/licenses/gpl-3.0.html

Die GPL gilt nur für den entsprechend lizenzierten eigenen Projektcode; bei der Firmwareweitergabe sind zusätzlich die Pflichten aus LGPL-2.1 und anderen Fremdlizenzen zu erfüllen. Ein bloßes Beilegen von Lizenztexten ersetzt diese Pflichten nicht.
