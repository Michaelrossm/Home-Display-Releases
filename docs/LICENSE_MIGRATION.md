# Umstellung auf GPL-3.0-only

Der eigene Home-Display-Quellcode (einschließlich der im Repository enthaltenen CO₂-Waage) wird ab diesem Quellcode-Stand unter GNU GPL Version 3.0 **only** angeboten, soweit die jeweiligen Dateien nicht ausdrücklich andere Rechteinhaber oder Lizenzen nennen. Copyright © 2026 Michael Roßmann.

## Abgrenzung

- Bereits veröffentlichte v0.1.0-Images, signierte ZIPs, Manifeste und deren ursprüngliche Lizenzangaben bleiben historische Veröffentlichungsartefakte. Ein Commit ersetzt weder diese Dateien noch ihre Signaturen.
- Die Firmware zeigt den eingebetteten Lizenztext aus `LICENSE.md` und den Hinweis in `src/ui/LicenseUi.cpp`. **Erst nach einem erneuten Firmwarebuild und einer gesonderten Veröffentlichung** erscheint die neue Lizenz auch auf Geräten. Bestehende Binärdateien werden nicht durch diesen Commit verändert.
- Bibliotheken und sonstige Fremdsoftware behalten ihre Original-Lizenzen, siehe `THIRD_PARTY_NOTICES.md` und `licenses/third-party/`.
- GPLv3 erlaubt kommerzielle Nutzung, verlangt bei Verbreitung des abgeleiteten GPL-Programms aber die entsprechenden Lizenz- und Quellcodepflichten.

## Vor einem neuen Binärrelease klären

1. Rechte an allen selbst lizenzierten Dateien und Beiträgen bestätigen. Fremdcode darf nicht eigenmächtig umgelabelt werden.
2. LGPL-2.1-Anforderungen für `libsmb2` und relevante Arduino-ESP32-Teile klären: Bibliotheksquellcode, Änderungen, Austausch-/Relink-Möglichkeiten sowie erforderliche Unterlagen.
3. Bei als User Product einzustufenden Geräten GPLv3-Anforderungen an Installation modifizierter Firmware und die Update-Signaturpraxis prüfen. Kein privater Signaturschlüssel muss veröffentlicht werden, aber notwendige Installationsinformationen können erforderlich sein.
4. Tatsächlich verwendete Schriftarten, Codecs, Patente und NOTICE-Texte gegen das finale Binary prüfen.
5. Bei Veröffentlichung eines neuen Builds Lizenztext, Lizenzanzeige, Paketmanifest, Source-Angebot und Hashes gemeinsam neu erzeugen. Historische v0.1.0-Artefakte nicht stillschweigend ersetzen.
6. Falls die private Entwicklungsquelle separat weitergeführt wird, muss dieselbe Lizenzumstellung dort bewusst synchronisiert werden; dieses öffentliche Repository aktualisiert sie nicht automatisch.

Diese Dokumentation ist keine anwaltliche Freigabe.
