# Diagnose-Werkzeuge

Kleine Python-Skripte, die sich per ESPHome-API direkt mit dem Controller
verbinden. Sie lesen `secrets.yaml` aus dem Projektwurzelverzeichnis und
erwarten den Controller unter `192.168.1.12`.

Voraussetzung: `pip install aioesphomeapi pyyaml`

Aufruf immer aus dem Projektwurzelverzeichnis, damit `secrets.yaml` gefunden
wird, zum Beispiel:

```
python tools/check_entity_states.py
```

| Skript | Zweck |
|---|---|
| `check_entity_states.py` | Liest alle Sensoren, Binaersensoren und Textsensoren einmalig aus. Schneller Gesamtcheck nach einem Flash. |
| `check_lift_status.py` | Kompakter Lift-Status: Referenz, Zustand, Position in Pulsen, mm und Prozent, Coverposition. |
| `dump_entity_ids.py` | Listet alle Entitaeten mit der Home-Assistant-Entity-ID. Grundlage fuer Dashboards, damit keine veralteten IDs verwendet werden. |
| `check_light_effects.py` | Zeigt die Effektlisten beider Strips und die Optionen der Szenenauswahl. |
| `watch_lift_logs.py` | Haengt sich 25 s an den Logstream und filtert Lift- und Motormeldungen. Zum Mitlesen waehrend einer Fahrt. |
| `encoder_diagnose.py` | Loest die Encoder-Diagnose aus und liest das Ergebnis mit. ACHTUNG: der Lift faehrt dabei kurz aufwaerts. |
| `verify_dashboard_entities.py` | Vergleicht jede im Dashboard referenzierte Entitaet mit der Entitaetsliste des Geraets. Trennt Dashboard-Tippfehler von Problemen der Home-Assistant-Registry. |
| `test_persistence.py` | Setzt die Referenz, startet den Controller neu und prueft, ob Referenz und Position aus dem NVS zurueckkommen. Bewegt keine Hardware. |

## Home-Assistant-Pipeline

`ha.py` faellt aus dem Rahmen der Liste oben: es redet nicht mit dem
Controller, sondern mit Home Assistant, und rollt das ganze Setup aus.

```
python tools/ha.py check      Zugang pruefen (Netz, SSH, Token)
python tools/ha.py keygen     SSH-Schluessel fuer den Zugang anlegen
python tools/ha.py setup      Erstinstallation komplett ausrollen
python tools/ha.py deploy     Dashboards ausrollen
python tools/ha.py verify     Dashboards gegen die HA-Registry pruefen
python tools/ha.py --help     alle Kommandos
```

Zugangsdaten stehen in `secrets.yaml` unter `ha_host`, `ha_token` und
`ha_ssh_key` (Vorlage: `secrets.example.yaml`). Anleitung inklusive
Neuinstallation: [`docs/home-assistant-pipeline.md`](../docs/home-assistant-pipeline.md).

Merkhilfe zur Abgrenzung: `verify_dashboard_entities.py` fragt **das Geraet**,
ob es eine Entitaet liefert. `ha.py verify` fragt **Home Assistant**, ob es sie
kennt. Erst beide Antworten zusammen sagen, wo ein Problem sitzt.

Hinweis: `encoder_diagnose.py` bewegt Hardware. Vorher pruefen, dass nach oben
Weg frei ist.
