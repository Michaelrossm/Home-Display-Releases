# Goliath Phase 1 – Videobasis (11.10.2026)

Basis: Hauptprojekt-ZIP + Kalender-Stabilitätsfix (unverändert übernommen).

- VideoService ergänzt RTSP Digest MD5 (RFC 2617, qop=auth oder ohne qop) nach 401-Challenge, mit Basic-Fallback, ohne Zugangsdaten in Logs. Nicht unterstützt: MD5-sess, SHA-256, mehrere verschiedene 401-Challenges.
- Native GoliathService-API: startCamera / stopCamera / cameraSnapshot / cameraFrame; nutzt vorhandene ConfigService-Secrets und den zentralen VideoService, H.264 Unterstream 352x288, maximal 5 veröffentlichte FPS. Kein permanenter Kameraabruf im Hintergrund.
- Lua-VideoBridge meldet rtsp_digest=true.

## Ehrliche Einschränkung

**Das ist der Videobackend-Patch, noch keine fertig bedienbare Goliath-Livebildseite.** Die vorhandene Goliath-App hat derzeit nur die HTTP-Probe, keine native Bildansicht. Um das Bild wirklich zu sehen, muss die Appoberfläche die neue `cameraFrame()`-API nutzen oder eine separate Lua-App die bestehende `hp_video_*`-Schnittstelle verwenden. Die native Goliath-Kamera ist nicht automatisch beim Öffnen aktiviert.

Die Kamera und das Gerät wurden nicht getestet; kein Hardware-Build in dieser Umgebung. RTSP-Profil und Digest-Protokoll müssen auf der echten Außenstation geprüft werden. Kein Klingeln, Mikrofon, Gespräch oder Türöffnen in diesem Paket.

Kein Zugangsdaten-Hardcoding. Kein Schreibzugriff auf die Außenstation. Bestehende Innenstationen unangetastet.
