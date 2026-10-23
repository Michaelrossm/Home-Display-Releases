# Sporadischer Bildschirmschoner – Race-Fix

Ursache war kein zweiter Screensaver-Aufruf.

`IdleService::activity()` kann aus dem LVGL-/Touch-Task kommen, während
`IdleService::loop()` im Haupttask gleichzeitig die Inaktivitätszeit prüft.
`lastActivity_` und `stage_` waren normale Variablen.

Dadurch konnte folgende Reihenfolge auftreten:

1. Idle-Loop beginnt mit altem Aktivitätsstand.
2. Benutzer berührt den Bildschirm.
3. `activity()` setzt Active und einen neuen Zeitstempel.
4. Der bereits laufende Idle-Loop beendet seine alte Entscheidung und setzt
   unmittelbar danach wieder Dimmed/Saver/Off.

Fix:

- atomarer Aktivitätszeitstempel
- atomarer Idle-State
- atomarer Context
- Aktivitäts-Generation
- Generation wird direkt vor jeder Timer-Transition erneut geprüft
- Timestamp wird zusätzlich erneut geprüft
- eine Benutzeraktivität gewinnt damit immer gegen eine ältere Timerentscheidung

`Saver` und `Off` können weiterhin ausschließlich durch den konfigurierten
Idle-Timer erreicht werden. Eine neue Aktivität setzt sofort auf Active zurück.
