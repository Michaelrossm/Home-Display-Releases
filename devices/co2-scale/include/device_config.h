#pragma once
// GPIOs an DEINE Platine anpassen. HX711 an 3,3 V betreiben.
#define PIN_HX711_DOUT 21
#define PIN_HX711_SCK  22

// Der Webserver ist ab Werk ohne Passwort. Optional in der Weboberflaeche aktivieren.
#define WEB_ADMIN_USER "admin"

// Anzeigename im Router/DHCP. Keine Leerzeichen verwenden.
#define WIFI_HOSTNAME "CO2-Waage"

// Ein WPA2-Hotspot kann KEIN Passwort mit nur 3 Zeichen verwenden.
// Stattdessen wird ein offener Einrichtungs-Hotspot namens ESP32-CO2-XXXXXX gestartet.
#define FIRMWARE_VERSION "1.0.13"
#define NTP_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
