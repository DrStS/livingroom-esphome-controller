# SMA-Speedwire-Proben (fremdes Thema, archiviert)

Diese Skripte gehoeren **nicht** zum Wohnzimmer-Controller. Sie horchen im
Netz nach SMA-Wechselrichtern und Energy Metern (Speedwire, UDP-Multicast
239.12.255.254:9522) und protokollieren, welche Geraete antworten.

Aufbewahrt, weil die Messprotokolle nachvollziehbar bleiben sollen. Der Versuch,
die Wechselrichterdaten anzubinden, ist **nicht weiterverfolgt worden** -- es
kam nichts Brauchbares zurueck. Wer hier wieder anfaengt, sollte mit einer
fertigen Integration starten (`pysma` oder die SMA-Integration in Home
Assistant) statt das Protokoll selbst zu zerlegen.

| Datei | Zweck |
|---|---|
| `sma_speedwire_scan.ps1` | Horcht auf dem Multicast und gruppiert die Absender nach SUSyID und Seriennummer. |
| `sma_barn_probe.ps1` | Gezielte Probe auf ein einzelnes Geraet, protokolliert die Antworten als Hex. |
| `sma_inventory.txt` | Ergebnis eines Scan-Laufs: gefundene Geraete. |
| `sma_barn_probe.txt` | Ergebnis eines Probe-Laufs. |

Die Protokolle enthalten Geraeteseriennummern und lokale IP-Adressen, aber keine
Zugangsdaten.

Nichts davon beruehrt den Controller oder Home Assistant. Die Skripte lesen nur
mit und senden keine Steuerbefehle.
