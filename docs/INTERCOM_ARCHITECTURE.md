# Gemeinsame IP-Sprechanlagen-Infrastruktur

Aktuelle native Oberfläche, HTTP-Digest, Wiederverbindungen und Verifikationsgrenzen:
[NATIVE-INTERCOM.md](NATIVE-INTERCOM.md). Die folgenden ursprünglichen Messwerte und
„offen“-Angaben beschreiben den damaligen Infrastrukturstand, nicht den neuen Build.

## Einbindung und Grenzen

Die vorhandenen Dienste bleiben Eigentümer von Netzwerk, UI, Secrets und Audiohardware.
`IntercomService`, `VideoService` und `IntercomAudioService` werden in `AppServices`
initialisiert. Es entstehen **keine zusätzlichen dauerhaften Tasks**. RTSP-Empfang läuft
als Maintenance-Schritt im vorhandenen NetworkWorker, Dekodierung im vorhandenen
Transferworker, LVGL ausschließlich unter DisplayUiGuard im bisherigen UI-Task.
Es gibt weder eine zweite LVGL-Instanz noch einen zusätzlichen Webserver.

Die native Goliath-App, ihre Einstellungen und HTTP-Verbindungsdiagnose bleiben erhalten.
Ihre Diagnosecodes werden zusätzlich an den gemeinsamen RAM-Ring weitergegeben.
Ein erreichbarer HTTP-Port ist **kein Nachweis für Gegensprechen**. Die bisherigen
Goliath-Funktionen erhalten keine erfundenen Annahme-/Türöffner-Fähigkeiten.

## Video

Implementiert: RTSP DESCRIBE/SETUP/PLAY mit RTP über TCP-Interleaving und OPTIONS-
Keepalive; H.264 Single-NAL, STAP-A und FU-A; HTTP-JPEG/MJPEG; gemeinsame RGB565-
Frames für LVGL ohne zusätzliche Renderkopie. Baseline-SPS, Auflösung und Referenzzahl
werden vor der Dekodierung geprüft. Bei Verlust/Überlast wird bis zum nächsten IDR
resynchronisiert. Veraltete Session-Ergebnisse werden anhand der Generation verworfen.

Softwaredecoder: gepinntes offizielles
[Espressif esp_h264 1.4.0](https://components.espressif.com/components/espressif/esp_h264/versions/1.4.0/readme?language=en).
JPEG verwendet den ESP-IDF-P4-Hardwaredecoder. Dual-Task-Decoder und Decoder-IRAM
sind deaktiviert. Keine H.265-/AV1-Implementierung.

Ein aktiver Stream, bis 640×480, progressive H.264 Baseline, höchstens zwei Referenzen,
1–10 veröffentlichte FPS (Standard 5). Das FPS-Limit reduziert veröffentlichte Frames,
nicht automatisch die Sendefrequenz der Kamera. Der Streampfad wählt das Kameraprofil.
Nur private IPv4-Ziele, keine Redirects; Hostnamen werden auf eine geprüfte IP aufgelöst.
RTSP/HTTP sind unverschlüsselt: ausschließlich vertrauenswürdiges LAN verwenden.
Unterstützt ist Basic-Authentifizierung; Digest, RTSPS, RTP/UDP, RTSP-Audio,
HTTP-Chunked-MJPEG und herstellerspezifische Anmeldung sind ausdrücklich offen.

Empfang: maximal 8 KiB bzw. 3 ms pro Schritt, 12 KiB RTSP-Header, 8 KiB SDP,
64 KiB Interleaving-Paket, 192 KiB komprimierter Frame. Handshake-/Keepalive-Deadline
4 s, Datenstillstand 8 s, Connect-Timeout 500 ms. DNS und bereits laufende Codec-/
BSP-Aufrufe sind nicht hart unterbrechbar. Das Decoder-Schleifenbudget von 150 ms
ist kooperativ, kein garantierter Maximalwert eines einzelnen Codec-Aufrufs.

## Audio und Rufzustände

ES8311-Mikrofon und Lautsprecher ausschließlich über den bestehenden Waveshare-BSP/
AudioHardwareService. Mikrofon wird erst bei Gesprächsbeginn geöffnet und anschließend
geschlossen. Ein lazy BSP-Handle bleibt erhalten, damit wiederholte Gespräche keine
BSP-Interfaces verlieren; RX wird geschlossen, ohne den gemeinsamen Lautsprechercodec
auszuschalten. PCM16/mono/16 kHz; G.711 A-law/µ-law mit 8-kHz-Konvertierung, Empfang
bis 320 Samples und Aufnahme bis 160 Samples je Aufruf. Aufrufe sind nur im vorhandenen
Worker zulässig. Die Codec-Read/Write-API bietet hier keine harte Abbruchgarantie.

Eine zentrale Intercom-Audioquelle übernimmt den globalen Fokus. Eine vorher spielende
Quelle wird pausiert und nach Gesprächsende fortgesetzt, sofern weder eine neu gewählte
Quelle noch ein PIN-Lock entgegensteht. Fokuswechsel aus der UI warten nicht auf
Audio-I/O; sie werden gegebenenfalls zunächst abgelehnt und der Stop wird eingereiht.

Zustände: Offline → Ready → Ringing → Connecting → Conversation → Ended/Error.
Vier Geräte und vier firmwareeigene Provider sind möglich. Der Provider meldet echte
Capabilities und führt Befehle ausschließlich im NetworkWorker aus. Er muss eigene
Netzwerkoperationen mit endlichen Timeouts begrenzen. Befehle werden nicht automatisch
wiederholt. Das Providerobjekt muss für die gesamte Firmwarelaufzeit gültig bleiben.

Der implementierte `lan_event`-Provider verarbeitet authentifizierte Ruf-/Ende-Ereignisse;
er behauptet keine Annahme-, Gegensprech- oder Türöffner-Unterstützung. SIP-Server und
unbestätigte Dahua/Goliath-/Hikvision-/DoorBird-Adapter wurden nicht hinzugefügt.
RTP-Parser/G.711 und Providervertrag bilden die wiederverwendbare Protokollgrundlage.

Das native Ruf-Overlay weckt das Display, ohne die aktuelle App zu ersetzen. Es liegt
auf der Systemebene, auch bei geschlossenem App-UI. PIN schützt sensible Aktionen;
Türöffnung erfordert Providerfähigkeit, Berechtigung, aktives Gespräch und Bestätigung
(natives Overlay: zweiter Tastendruck innerhalb 5 s). Doppelte Ruf-/Ende-Ereignisse
sind idempotent; Rufzeit ist auf 60 s begrenzt. App-Schließen beendet Gespräche, nicht
die Rufbereitschaft aktivierter Geräte. Disable/Uninstall invalidieren betroffene Runs.

## Lua, Einstellungen und Web

Neue Schema-3-Berechtigungen: `camera`, `microphone`, `intercom`, `door.open`.
Zusätzlich bleiben `network`, bei Aufnahme `audio`, bei Secret-Schreiben `secrets`
erforderlich. Web- und Firmware-Manifestprüfung verwenden dieselben Namen.

- `hp_intercom_capabilities()` – ehrliche Feature-/Hardware-/Berechtigungsangaben.
- `hp_intercom_configure({id, provider, name, participant, host, port, username,
  password?, enabled})` – owner-scoped asynchroner LuaJob, Ergebnis über die bestehenden
  Job-APIs; PIN-/Settings-Guard, vier persistierte Geräte, Passwort im Secret-Store.
- `hp_intercom_device(id)` – eigene gespeicherte Einstellungen, niemals Passwort.
- `hp_intercom_snapshot()` – eigener aktueller Ruf, Call-Nonce/Revision/Capabilities.
- `hp_intercom_command(action, call, value?, confirmed?)` – `answer`, `hangup`,
  `mute`, `door`; ausschließlich Queue-Request, keine direkten Netzwerk-/LVGL-Aufrufe.
- `hp_video_open({host, path, port?, transport="rtsp"|"mjpeg", width?, height?,
  fps?, username?, secret?})` – expliziter Vordergrundstart. `secret` bezeichnet einen
  eigenen vorhandenen Lua-Secret, keinen fremden NVS-Schlüssel.
- `hp_video_stop()`, `hp_video_snapshot()`, `hp_video_view(parent)` – eigener Stream;
  maximal vier persistierende LVGL-Image-Views, keine Objektneuanlage pro Frame.

Die Funktionen verwenden den bestehenden LuaBridgeContract. Konfiguration und
Videostart sind im gesperrten/Background-UI nicht erlaubt; sensible Befehle bleiben
PIN-geschützt. Geparkte Apps werden nicht sichtbar fortgesetzt. Suspend/Close/
Quarantäne/Owner-Cleanup schließen Video und verwerfen verspätete Ergebnisse.
Bestehende Apps benötigen keine Änderungen; neue Settings-Oberflächen verwenden den
bisherigen getrennten App-Settings-/KV-/Web-Settings-Pfad statt einer zweiten Verwaltung.

Port 80: `GET /api/intercom/diagnostics` ist authentifiziert;
`POST /api/intercom/event` verwendet die vorhandene Authentifizierung, CSRF- und
CSRF-Prüfung. Rufanzeige darf auch bei Display-PIN erfolgen, sensible Aktionen nicht.
JSON: `{device, call, event:"ring"|"end"}`, maximal 256 Byte.
Nur konfigurierte `lan_event`-Geräte und berechtigte aktivierte App-Owner sind zulässig.
Dies ist ein geschützter generischer Eingang, keine zugesagte Hersteller-Kompatibilität.
Der Recovery-Server auf 8088 veröffentlicht diese Endpunkte nicht.

Diagnose: 32 RAM-only-Einträge mit Zeitstempel und festen Codes sowie Audio-/Video-
Zustand, Frame-/Drop-/Fehlerzähler; keine Passwörter, URLs oder Flash-Logs.

## Ressourcen und Verifikation

Frames: höchstens drei RGB565-Bilder, konservativ maximal 1.843.200 Byte PSRAM;
zusätzlich 192 KiB Empfangsframe + 64 KiB RTP-Paket + höchstens ein 192-KiB-Decodejob,
begrenzte Header/Parameter-Sets, Decoder-Arbeitsspeicher und kleine Verwaltungsobjekte.
Decoder-Arbeitsspeicher ist nicht im Framebudget enthalten; die SPS-Grenzen begrenzen
das Format, ersetzen aber keine auf Hardware gemessene Peak-Heap-Zahl. Audio hat
640 PCM16-Samples/1.280 Byte festen Puffer, kein permanentes Capture im Leerlauf.
Im Leerlauf bleiben nur Dienste/Registry/Profil-/Diagnosedaten; keine Videoverbindung.

Hosttests prüfen G.711, RTP-Grenzen, H.264-Reassembly/Verlust/Wraparound, SPS-Grenzen
und Integration/Sicherheits-/Task-/Owner-Guards. Sie ersetzen keine Live-Streamtests.
Verifiziert vor dem anschließenden Kalender-UI-Umbau: 247 Hosttests und vier
Architekturtests grün; source-only Release-Validator grün (0.3.3b); R3/400-MHz-Build
grün. Statisches RAM 127.896 Byte, Flash 5.445.644 Byte gegenüber R3-Ausgangsstand
122.144 / 5.268.046 Byte: +5.752 Byte RAM, +177.598 Byte Flash. Neue dauerhafte
Tasks: 0. Das sind Linkerwerte, keine gemessenen Laufzeit-Peaks oder FPS-Werte.

Offen auf echter R3-Hardware: Mikrofonverfügbarkeit/Vollduplex, Audioformatwechsel und
Musikfortsetzung, Kamerafarben/Baseline-Streams, Peak-PSRAM, FPS/UI-Latenz unter Last,
CSRF-Rufsender, Klingeln bei PIN und geschlossener App. Für vollständiges Gegensprechen
fehlen bestätigte Geräteanmeldung, Ring-Event-Protokoll, Audio-RTP-Transport/Negotiation,
Annahme/Beenden und Türöffneradapter. Kein reales Gerät wurde verändert oder geflasht.
