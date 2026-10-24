# WLAN-Einrichtung v1.0.13 (ESP32-WROOM-32)

Bei Erststart erstellt die Waage den offenen Hotspot `ESP32-CO2-XXXXXX` (ohne Kennwort). Mit einem Handy oder PC verbinden; meistens erscheint das Einrichtungsfenster automatisch, ansonsten `http://192.168.4.1` öffnen. Die Seite scannt 2,4-GHz-WLANs, lässt Netzwerk und Passwort wählen und speichert die Zugangsdaten. Nach erfolgreicher Verbindung schaltet die Waage nach ca. 10 Sekunden in den WLAN-Client-Modus. Die neue IP-Adresse im Router nachsehen. Bei falschem Passwort bleibt der Einrichtungs-Hotspot verfügbar. Wenn das gespeicherte WLAN beim Booten nicht erreicht wird, aktiviert sich nach 25 Sekunden ebenfalls der Hotspot.

**Standardzugang:** WLAN-Einrichtungshotspot ohne Passwort. Der Webserver hat standardmäßig keine Anmeldung; optional kann in der Weboberfläche ein Passwort gesetzt werden (Benutzer `admin`). Ein WPA2-Passwort `co2` mit nur drei Zeichen ist technisch nicht erlaubt. Nur im vertrauenswürdigen Heimnetz betreiben und keine Router-Portfreigabe einrichten. Das Gerät besitzt nur 2,4-GHz-WLAN. WLAN-Suche läuft nur im Hotspot-Modus. Der konfigurierte Nachtzeitplan schaltet nach erfolgreicher Zeitsynchronisierung auch den Hotspot ab. Das JSON-API ist unter `http://<ip>/api/v1/status` erreichbar. GPIO 21 = HX711 DOUT, GPIO 22 = HX711 SCK. Es bleibt bei einer Messung je 60 Sekunden.

---

# CO₂-Flaschenwaage – ESP32 / HX711 / PlatformIO

Komplettes PlatformIO-Projekt für einen **klassischen ESP32 DevKit** (Board `esp32dev`). Keine Cloud, kein externer Webserver.

## Anschlüsse

| HX711 | ESP32 |
|--|--|
| VCC | **3V3** |
| GND | GND |
| DT / DOUT | GPIO 21 |
| SCK / CLK | GPIO 22 |

Die 4 Drähte der Wägezelle entsprechend **E+, E−, A+, A−** am HX711 anschließen (Farbcodes variieren!). **Keine 5-V-Ausgänge an ESP32-GPIOs** anschließen. Die Wägezelle mechanisch einseitig fest und einseitig an der Plattform montieren, frei beweglich, ohne Reibkontakt. Flaschenschlauch möglichst zugfrei führen.

## Flashen

1. Der Einrichtungs-Hotspot hat kein Passwort. Nach Einrichtung lässt sich auf der Weboberfläche optional ein Administratorpasswort setzen.
2. GPIOs prüfen (bei anderen ESP32-Modellen anpassen).
3. In VS Code mit PlatformIO den Ordner öffnen, `Build` und `Upload` ausführen.
4. Serielle Konsole: 115200 Baud.
5. Ohne konfigurierte WLAN-Zugangsdaten mit dem offenen Hotspot `ESP32-CO2-XXXXXX` verbinden und `http://192.168.4.1` öffnen.
6. WLAN-Zugangsdaten speichern. Danach im Heimnetz die IP-Adresse über Router oder serielle Konsole ermitteln und `http://<IP>` aufrufen.
7. Standardmäßig ohne Anmeldung auf die Weboberfläche zugreifen. Optional im Abschnitt „Webserver-Passwort“ einen Passwortschutz aktivieren; Benutzer ist dann `admin`.

## Erste Messung

1. **Plattform frei**: Button „Nullpunkt“ drücken. Dabei darf noch keine Flasche daraufstehen.
2. Ein **bekanntes Gewicht** (z. B. 200 g) mittig aufstellen. Genau dessen Grammzahl eingeben und „Messpunkt aufnehmen“ drücken.
3. Das Prüfgewicht entfernen, ein **anderes Prüfgewicht** (z. B. 500 g, 1 kg, 2 kg oder 5 kg) auflegen und einen weiteren Messpunkt aufnehmen. Insgesamt sind **bis zu acht Prüfgewichte** möglich. Alle Prüfgewichte sind **Gesamtgewichte auf der Plattform**, keine Differenzwerte.
4. Unter „Gespeicherte Prüfgewichte“ kannst du einzelne Punkte wieder entfernen. Der Nullpunkt löscht alle Prüfgewichte und beginnt eine neue Kalibrierung.

Die Firmware interpoliert linear zwischen den Messpunkten, um systematische Nichtlinearität der Wägezelle teilweise zu korrigieren. Außerhalb der aufgenommenen Punkte wird extrapoliert; deshalb nach Möglichkeit den tatsächlich genutzten Gewichtsbereich abdecken. Alle Punkte werden im ESP32-NVS gespeichert.

Eine bestehende Einpunkt-Kalibrierung aus älteren Firmwareständen wird weiterhin gelesen. Sobald du den ersten neuen Messpunkt speicherst, wechselt die Waage auf die neue Mehrpunkt-Kalibrierung.
3. Kalibriergewicht entfernen, Flasche aufstellen.
4. Das Eigengewicht der **komplett leeren CO₂-Flasche** und separat des dauerhaft mitgewogenen Druckminderers in Gramm hinterlegen. In der Weboberfläche sind `425 g`, `2000 g` und `3000 g` direkt auswählbar; beliebige andere Nennfüllmengen können ebenfalls eingegeben werden.
5. Ggf. mit einer Waage gegenprüfen. Vermeide Druck / Zug durch angeschlossenen Schlauch.

Rechenweg:
- `gesamt_g` = Gewicht auf Wiegeplattform (berechnet aus Rohsignal und Kalibrierfaktor)
- `gas_unclamped_g` = `gesamt_g - leere_flasche_g - druckminderer_g`
- `restgas_g` = `max(0, min(gas_unclamped_g, nennfuellmenge_g))`
- `fuellstand_prozent` = `100 * restgas_g / nennfuellmenge_g`

Hinweis: Die tatsächliche Füllung kann von der Nennfüllmenge abweichen; ein negativer oder zu hoher Roh-Gaswert wird als Warnung angezeigt. **Keine automatische Nullstellung mit aufliegender Flasche** durchführen.

Nullpunkt, Kalibrierfaktor und Einstellungen werden im nichtflüchtigen ESP32-NVS gespeichert und bleiben nach einem Stromausfall erhalten. Nach dem Neustart wird das aktuelle Gewicht neu gemessen; die Flasche darf dabei auf der Plattform stehen. Zum erneuten Tarieren muss die Plattform dagegen leer sein.

## WLAN-Nachtmodus und Uhrzeit

Das WLAN kann z. B. täglich von **22:00 bis 06:00** deaktiviert werden. In dieser Zeit wiegt das Gerät weiter, aber es kann **nicht** per Webseite oder P4 abgefragt werden. Bei der Wiederaktivierung wird der Webserver erneut gestartet. NTP setzt die Uhrzeit (Europe/Berlin inklusive Sommerzeit); alternativ kann die Browserzeit über „Gerätezeit setzen“ übernommen werden. Die Uhr läuft bei laufendem ESP32 ohne WLAN weiter. **Nach einem Neustart ohne zuvor gültige Zeit** wird WLAN zum Zeitsynchronisieren zunächst eingeschaltet; ein internetfreies Netz ohne NTP benötigt die Zeitübertragung vom Browser oder später einen lokalen NTP-Server. Zeitplan ist initial **aus**, damit keine versehentliche Abschaltung erfolgt.

**Kein Deep Sleep:** Messung läuft weiter. Messwerte werden aktuell **nicht historisiert**; nach einem WLAN-freien Zeitraum liefert die API wieder den aktuellen Stand. ESP32 darf nicht stromlos werden, wenn er den Nachtzeitplan ohne Zeitsynchronisation fortführen soll.

## Für dein ESP32-P4 Home-Display

Read-only JSON-GET (im gleichen LAN):

`GET http://<ESP32-IP>/api/v1/status`

Beispieldaten (nur zur Illustration, keine echte Messung):

```json
{
  "schema": 1,
  "name": "CO2 Waage",
  "version": "1.0.13",
  "measurement": {
    "valid": true,
    "sensor_ready": true,
    "calibrated": true,
    "gross_g": 6400,
    "gas_g": 1900,
    "gas_unclamped_g": 1900,
    "percent": 95,
    "empty_g": 4000,
    "regulator_g": 500,
    "capacity_g": 2000,
    "warning": ""
  },
  "wifi": {"mode": "station", "ip": "192.168.1.50", "rssi": -55, "scheduled_off": false},
  "time": {"valid": true, "epoch": 1792418400, "local": "2026-10-19 16:00:00"},
  "uptime_ms": 234567
}
```

Nur `measurement.valid == true` als gültige Messung darstellen. In der P4-App `gas_g`, `gross_g`, `percent`, `measurement.warning`, `time.epoch` verwenden. Abfrage alle **15–60 Sekunden** genügt. Während der Nacht oder bei Verbindungsfehlern `offline` anzeigen und nicht den alten Wert als aktuell präsentieren. Auf Wunsch feste DHCP-Reservierung im Router vergeben.

## Sicherheit / Grenzen

- **Keine Cloud** und keine Internet-Portfreigabe machen.
- HTTP Basic Auth schützt **Schreib-Endpunkte und Konfiguration**, nicht die Status-GET-API, damit das P4 die Werte unkompliziert lesen kann.
- Die HTTP-Verbindung ist **nicht TLS-verschlüsselt**; nur vertrauenswürdiges lokales WLAN nutzen.
- Ein Webbrowser mit gespeicherter Basic-Authentifizierung darf von einem anderen lokalen Rechner nicht als Ersatz für Netzsegmentierung betrachtet werden.
- Die Status-API enthält keine Zugangsdaten. WLAN-Passwort wird im NVS-Flash abgelegt und nicht über die API zurückgegeben.
- Die erste WLAN-Einrichtung erfordert die Verbindung mit dem ESP-Hotspot; ein Captive-Portal-DNS und Erkennungsseiten sind implementiert, die automatische Anzeige hängt vom Endgerät ab.
- 10-kg-Zelle: gesamtes Gewicht inklusive Zubehör muss < 10 kg bleiben. Stoßbelastung berücksichtigen.

## Projektstruktur

- `platformio.ini` – PlatformIO Build
- `include/device_config.h` – Pins und Administrations-/Hotspot-Kennwort
- `src/main.cpp` – Waage, Webserver, Einstellungen, WLAN und Zeitplan
- `src/index_html.h` – responsive Weboberfläche (im Flash, kein SPIFFS-Upload nötig)

Version **1.0.13**. Beim ersten Flashen an reale Hardware und Messverhalten anpassen und prüfen. Es erfolgte kein echter ESP32-Hardwaretest.


## Version 1.0.13 – Energiesparmodus
- CPU im Leerlauf 80 MHz, während des kurzen HX711-Messzyklus 160 MHz.
- HX711: ein Ergebnis alle 60 Sekunden, berechnet aus 8 Rohwerten; kein permanentes Abfragen mehr.
- WLAN-Modem-Sleep aktiviert, soweit vom Access Point unterstützt; WLAN-Zeitfenster bleibt erhalten.
- Web-UI aktualisiert sich alle 15 Sekunden, zeigt bis zur nächsten Messung denselben Wert.
- JSON-API: `power.cpu_mhz`, `power.wifi_modem_sleep`, `power.sample_period_seconds`, `power.sample_age_seconds`, `power.sampling`.
- Ohne Strom sparenden externen Spannungsregler ist die Einsparung des gesamten Devkits begrenzt.
- Bei WLAN-Abschaltung ist das Gerät nicht über die API erreichbar.
- Kein Deep Sleep: Andernfalls wäre der Webserver im WLAN-Zeitfenster nicht durchgehend erreichbar.

## Passwortschutz (optional)

Ab Werk bleibt der Webserver ohne Passwort. Unter „Webserver-Passwort (optional)“ ist ein neues Passwort einstellbar; ein leeres Feld beim Speichern entfernt es wieder. Die Status-API bleibt bewusst ohne Anmeldung. Der Hotspot ist offen, da WPA2 mindestens acht Zeichen Passwort erfordert und `co2` nur drei hat. **Achtung:** Jeder in Funkreichweite kann einen offenen Einrichtungs-Hotspot benutzen und WLAN-Einstellungen verändern. Keinesfalls am Internet exponieren.


## Version 1.0.13 – HX711-Sleep, Uhrserver und OTA

- Standard-Nennfüllmenge **2.000 g** für eine 2-kg-Flasche (bereits gespeicherte Werte werden nicht überschrieben).
- **HX711 Power-Down** über SCK zwischen den Messzyklen; Aufwecken vor dem Zyklus. Weiterhin nur **ein Messzyklus alle 60 Sekunden** (8 Rohwerte innerhalb dieses Zyklus). Tare und Kalibrierung sind explizite zusätzliche Messaktionen.
- **CPU-Takt** 80 MHz im Leerlauf, 160 MHz während der Messung oder eines OTA-Updates. Bei abgeschaltetem WLAN zusätzliche echte **Light-Sleep-Intervalle** von 500 ms. Bei aktivem WLAN bleibt der HTTP-/NTP-Server erreichbar; WiFi-Modem-Sleep ist aktiviert. Kein Deep Sleep.
- `/api/v1/live` zeigt jetzt ausschließlich den letzten gespeicherten Wert an, keine zusätzlichen Sensorzugriffe.
- **Zeitquelle:** NTP-Client mit konfigurierbarem NTP-Server (Vorgabe `pool.ntp.org`, Ausweichserver `time.google.com`). Browser-Uhrzeit manuell setzen bleibt möglich. Zeitzone Europe/Berlin inklusive Sommerzeit.
- **Lokaler Uhrserver:** NTP auf **UDP 123** im Heimnetz, sobald eine gültige Uhrzeit vorhanden ist; zusätzlich `GET /api/v1/time` als JSON. Ohne gültige Zeit wird keine falsche NTP-Antwort gesendet. Der NTP-Dienst ist bei ausgeschaltetem WLAN nicht erreichbar.
- **OTA:** Auf der Weboberfläche `.pio/build/esp32dev/firmware.bin` auswählen und hochladen. Alternativ `POST /api/ota` mit multipart-Datei. Update wird in die inaktive OTA-Partition geschrieben, nach erfolgreichem Abschluss Neustart. **Erstinstallation per USB**. Firmware muss in die OTA-Partition passen; ESP32 mit passendem Partitionslayout verwenden.
- **Sicherheit:** Standardmäßig kein Webserver-Passwort; deshalb kann jeder im selben WLAN die Firmware aktualisieren. Nur im vertrauenswürdigen Netz verwenden und keine Portfreigabe einrichten. Das optionale Adminpasswort schützt auch OTA.
- **Wichtig:** Die Uhrzeit nach vollständigem Stromausfall benötigt erneute NTP-Synchronisation oder manuelle Zeitsetzung. Wenn nachts das WLAN aus ist, läuft die Uhr im eingeschalteten ESP32 weiter.
- **Tests:** Quellcode-/Strukturprüfung erfolgt; ohne vollständigen PlatformIO-Toolchain- und Hardwaretest sind Kompilierung, Stromverbrauch und OTA auf dem Gerät noch zu prüfen.


## Flaschengrößen und Gewichtsreferenzen (v1.0.13)

In der Weboberfläche gibt es Voreinstellungen für den **Quooker-Originalzylinder (425 g CO₂)** sowie 600 g, 1 kg, 2 kg, 3 kg, 5 kg, 6 kg und 10 kg. Die Voreinstellung legt **nur die CO₂-Nennfüllmenge** fest. Das Flaschen-Leergewicht ist nicht allgemeingültig und wird bewusst **nicht erfunden**: Es muss vom Tara-Aufdruck der tatsächlichen Flasche übernommen oder durch Wiegen ermittelt werden. Der Druckminderer wird separat erfasst.

Alle Eingaben sind veränderbar: Flasche leer, Druckminderer, Gasfüllmenge, Flasche voll, Gesamtgewicht leer und Gesamtgewicht voll. Die drei abgeleiteten Referenzgewichte sind direkt editierbar und passen die jeweils zugrunde liegenden Werte an. Gespeichert werden die drei unabhängigen Größen (Flaschen-Leergewicht, Druckminderer und Gas-Nennfüllmenge). Die übrigen Gewichte werden daraus berechnet. Bestehende Einstellungen bleiben nach dem Update erhalten. Firmwareversion bleibt **1.0.13**.


## Lizenz und Urheberrecht

Copyright © 2026 Michael Roßmann. Der eigene Projektcode steht unter **GNU GPL Version 3.0 (GPL-3.0-only)**. Die vollständige Lizenz steht in `LICENSE.md` und direkt auf der Waage unter `/license` sowie in der Weboberfläche unter **Lizenz und Urheberrecht**. Drittsoftware behält ihre jeweiligen Lizenzbedingungen; siehe `docs/legal/THIRD_PARTY_NOTICES.md` und `/third-party`. Die englische Originalfassung der Projektlizenz ist maßgeblich.

## Kalibrier-API (v1.0.13)

- `GET /api/calibration`: Nullpunktstatus und Liste der gespeicherten Referenzgewichte/Rohwerte.
- `POST /api/tare`: Nullpunkt bei leerer Plattform setzen, alle Kalibrierpunkte löschen.
- `POST /api/calibrate` mit `grams=500`: Messpunkt aufnehmen oder bei gleichem Gewicht aktualisieren.
- `POST /api/calibration/delete` mit `index=0`: einen Messpunkt entfernen.

Alle schreibenden Endpunkte unterliegen dem optionalen Webserver-Passwort, falls gesetzt. Während einer Kalibrieraufnahme werden 15 HX711-Rohwerte gemittelt. Die normalen Messzyklen bleiben auf einmal pro Minute begrenzt.

## Sicherheits-Härtung (Stand v1.0.13)

- Alle schreibenden HTTP-Endpunkte (WLAN, Zeit, Kalibrierung, Einstellungen, OTA)
  benötigen einen zufällig beim Boot erzeugten CSRF-Token. Die Weboberflächen
  beziehen ihn automatisch von `/api/security`.
- Browser-POSTs mit fremdem `Origin` werden abgewiesen.
- OTA funktioniert ohne Passwort. Ist ein Webserver-Passwort eingerichtet,
  verlangt OTA die entsprechende Anmeldung; außerdem erfolgt eine
  Prüfung auf `.bin`, eine Begrenzung der Uploadgröße und das Verwerfen fehlerhafter Updates.
- Live-Anzeige und Status-JSON bleiben ohne Anmeldung lesbar, damit Home-Display
  weiter integrierbar bleibt. Messwerte werden nur alle 60 Sekunden aufgenommen.
- Keine Cloud, kein offener Internet-Port. **Achtung:** HTTP Basic Auth
  verschlüsselt Passwörter NICHT. Firmware nur in einem vertrauenswürdigen
  Heimnetz betreiben, niemals Port 80/UDP 123 ins Internet weiterleiten.
- Einrichtungs-Hotspot bleibt offen, damit die Ersteinrichtung ohne zusätzliche
  Hardware möglich ist. Bei fremdem Zugriff in AP-Reichweite ist dies ein
  verbleibendes Risiko; AP nur für die Einrichtung verwenden.
- API-Clients mit schreibenden Aktionen müssen zuerst `/api/security` abfragen
  und `X-CSRF-Token` beim POST mitsenden. Nur lesende Home-Display-Clients
  brauchen keine Anpassung.

**Tests:** JavaScript-Syntax und ZIP-Inhalt geprüft. PlatformIO-Kompilierung
und ESP32-Hardwaretest konnten in dieser Umgebung nicht durchgeführt werden.

## Passwortschutz / öffentliche JSON-API

- Ohne eingerichtetes Webpasswort bleiben Oberfläche, Kalibrierung und OTA frei zugänglich (CSRF-Schutz für Änderungen bleibt).
- Sobald ein Passwort eingerichtet ist, erfordern **alle Weboberflächen und Verwaltungsendpunkte** HTTP-Basic-Anmeldung, einschließlich OTA, WLAN-Einrichtung, Kalibrierung, Einstellungen, Lizenzseiten und Token-Endpunkt.
- **Ausnahme: nur lesende JSON-Endpunkte** `/api/v1/status`, `/api/v1/live`, `/api/v1/time` sowie der kompatible Alias `/api/status` bleiben immer ohne Passwort erreichbar. So funktioniert die Home-Display-Anbindung unabhängig vom Webpasswort.
- Basic-Auth über HTTP ist nicht verschlüsselt: nur im vertrauenswürdigen LAN betreiben.


## WLAN-Rettungsmodus / OTA-Bootschutz (Reparatur vom 20.10.2026)

Diese Firmware behebt einen Fehler, bei dem nach dem OTA-Neustart bei aktivem
WLAN-Zeitplan weder Heim-WLAN noch der Einrichtungs-Hotspot sichtbar sein konnte.

- **120 Sekunden Start-Schutzfrist:** WLAN wird nach jedem Boot auch innerhalb
  des Abschaltzeitfensters gestartet. Danach gilt der Zeitplan wieder, sofern
  bereits eine erfolgreiche Verbindung zum Heim-WLAN bestand.
- **OTA-Rettungsstatus:** Nach einem erfolgreichen OTA-Upload wird ein
  `otarescue`-Flag in NVS gespeichert. Es wird erst nach mindestens 120 Sekunden
  erfolgreichem Betrieb mit Heim-WLAN oder aktivem Rettungs-Hotspot gelöscht.
  Solange es gesetzt ist, darf die Nachtabschaltung
  die Wiederherstellung nicht verhindern.
- **WLAN-Ausfall:** Bei ausbleibender Verbindung wird nach 30 Sekunden ein
  Einrichtungs-Hotspot gestartet (`ESP32-CO2-XXXXXX`, `192.168.4.1`).
  Der Hotspot bleibt auch während einer geplanten Nachtabschaltung verfügbar,
  solange das Heim-WLAN nicht wiederhergestellt werden konnte.
- **AP-Fehler:** Der Erfolg von `WiFi.softAP()` wird geprüft. Fehlgeschlagene
  AP-Starts werden alle 15 Sekunden erneut versucht, statt einen falschen
  `apActive`-Status zu behalten.
- **Funk-Abschaltung:** Die Nachtabschaltung wird nicht mehr durch den
  normalen WLAN-Neustart jede Sekunde wieder aufgehoben. Die Funkabschaltung
  bleibt bis zum Ende des Zeitfensters wirksam.
- **Während OTA:** Keine geplante WLAN-Abschaltung während des Uploads oder
  unmittelbar vor dem Neustart.
- **Statische IP:** Bei einer fehlerhaften Konfiguration wird DHCP versucht.
- **JSON:** `/api/v1/status` enthält zusätzlich `recovery` (OTA-Status,
  Fehlversuche, letzte Fehlerbeschreibung) und
  `wifi_schedule.effective_off`. Alle vier öffentlichen JSON-Endpunkte
  bleiben ohne Passwort erreichbar.

**Hinweis zur Wiederherstellung:** Diese Schutzmechanismen wirken erst, wenn
sie auf dem ESP32 installiert sind. Ein bereits nicht mehr erreichbares Gerät
kann dadurch nicht aus der Ferne repariert werden. Bei dauerhaft fehlendem
WLAN und AP muss die Firmware ggf. per USB **ohne Erase Flash** neu aufgespielt
werden. Dadurch bleiben die NVS-Einstellungen normalerweise erhalten.

**Grenzen:** Software-Rollback bei wiederholtem Neustart funktioniert nur,
wenn die Anwendung `setup()` erreicht. Ein Crash davor erfordert ggf. USB.
Kein Hardware-/PlatformIO-Buildtest in dieser Umgebung. Details siehe
`docs/RECOVERY.md`.

## Erweiterte Rettungsfunktionen und Backup (Build 1.0.13-recovery-2)

Die aktuelle Version erweitert den WLAN-Rettungsfix um einen RTC-Bootloop-Zähler,
einen **Software-Rückwechsel** auf die vorherige OTA-Partition nach drei
unbestätigten Boots (sofern eine gültige vorherige App vorhanden ist),
dynamische OTA-Größenprüfung anhand der Zielpartition, einen Boot-Health-Check
nach 120 Sekunden, ein Diagnoseprotokoll im RAM sowie JSON-Export/Import von
Flaschenprofil, Kalibrierung, Zeitplan und Zeitserver. Die Weboberfläche hat
entsprechende Bereiche. Die lesbaren JSON-Endpunkte bleiben ohne Passwort.

**Wichtige Einschränkung:** Das ist ohne passend konfigurierten ESP-IDF-
Bootloader **kein echtes Bootloader-Rollback** bei einem Absturz vor `setup()`.
Ein OTA-Partitionswechsel wird nicht automatisch erzwungen. Das Layout bleibt
unverändert. Siehe `docs/RECOVERY.md` für Installations- und Rettungshinweise.

**Vorherige README-Aussagen über fehlendes Rollback beziehen sich auf die alte
Version; für den neuen, eingeschränkten Software-Rückwechsel gelten die obigen
Angaben.**


## Neue Weboberfläche: Übersicht / Einstellungen

- **Übersicht:** große kreisförmige CO₂-Füllstandsanzeige, Restgas,
  Gesamtgewicht, Referenzgewichte, Kalibrierstatus, Alter der letzten
  Messung und HX711-Rohwert. Responsive Layout für Handy und Desktop.
- **Einstellungen → Flasche & Gewichte:** Flaschenprofile und sämtliche
  manuell überschreibbaren Gewichte.
- **Einstellungen → Kalibrierung:** Nullpunkt, Mehrpunkt-Kalibrierung
  und Verwaltung der Kalibrierpunkte.
- **Einstellungen → Netzwerk & Zeit:** WLAN, Nachtabschaltung,
  statische IP und NTP.
- **Einstellungen → System & Update:** Passwort, OTA, Diagnose,
  JSON-Backup, Home-Display-API und Lizenz.
- Keine zusätzliche HX711-Abfrage: Status-Refresh 15 s, Live-Cache 5 s,
  tatsächlicher Messzyklus unverändert bei 60 s.
- Das UI benutzt ausschließlich die bereits vorhandenen API-Endpunkte.
  JSON-Messwerte bleiben immer ohne Passwort lesbar.

Prüfung: JS-Syntax mit Node.js und HTML-ID/JS-Referenzen statisch
validiert; kein vollständiger PlatformIO-Build und kein Hardwaretest.
