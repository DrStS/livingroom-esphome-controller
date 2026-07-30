# Home-Assistant-Konfiguration

Alles hier ist die Quelle der Wahrheit. Nichts davon wird auf dem Host von Hand
bearbeitet -- ausgerollt wird mit der Pipeline:

```
python tools/ha.py check      Zugang pruefen
python tools/ha.py setup      Erstinstallation komplett ausrollen
python tools/ha.py deploy     Dashboards ausrollen (Alltagsfall)
```

Anleitung inklusive Neuinstallation: [`docs/home-assistant-pipeline.md`](../docs/home-assistant-pipeline.md)

## Aufbau

| Pfad | Ziel auf dem Host | Wird ausgerollt mit |
|---|---|---|
| `config/configuration.yaml` | `/config/configuration.yaml` | `deploy --core` |
| `config/automations.yaml` | `/config/automations.yaml` | `deploy --core` |
| `config/scripts.yaml` | `/config/scripts.yaml` | `deploy --core` |
| `config/scenes.yaml` | `/config/scenes.yaml` | `deploy --core` |
| `config/dashboards/livingroom.yaml` | `/config/dashboards/livingroom.yaml` | `deploy` |
| `deploy-manifest.yaml` | -- | steuert das Ausrollen |

`automations.yaml`, `scripts.yaml` und `scenes.yaml` sind bewusst leer: sie
werden in der Oberflaeche gepflegt. Deshalb fasst der normale `deploy` sie nicht
an, nur `--core` -- und auch dann liegt der bisherige Stand vorher auf dem Host
unter `/config/.deploy-backup/<zeitstempel>/`.

## Warum die Logik nicht hier liegt

Lift-Sicherheit, Positionsregelung und Lichteffekte laufen auf dem ESP32. Sie
dürfen nicht davon abhaengen, dass Home Assistant erreichbar ist. Home Assistant
zeigt an und bedient; entscheiden tut der Controller.
