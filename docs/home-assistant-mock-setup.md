# Home Assistant Mock Setup

Der hardwarefreie Mock steckt jetzt direkt in `livingroom.yaml` (finaler Name
`wohnzimmer-controller`). Er erzeugt exakt die endgültigen Entities, aber ohne
echte Hardware. Home Assistant hängt den Gerätenamen als Präfix an die IDs an,
daher tragen alle Controller-Entities das Präfix `wohnzimmer_controller_`.

## Was du in Home Assistant siehst

### Lift
- `cover.wohnzimmer_controller_tv_lift`
- `sensor.wohnzimmer_controller_tv_lift_position_percent`
- `sensor.wohnzimmer_controller_tv_lift_position_pulses`
- `binary_sensor.wohnzimmer_controller_tv_lift_homed`
- `binary_sensor.wohnzimmer_controller_tv_lift_fault`
- `button.wohnzimmer_controller_tv_lift_reference`
- `button.wohnzimmer_controller_tv_lift_clear_fault`

### Licht
- `light.wohnzimmer_controller_sideboard_ambient`
- `light.wohnzimmer_controller_cabinet_glass_edge`
- `light.wohnzimmer_controller_cabinet_inner_diffuser`
- `select.wohnzimmer_controller_livingroom_light_scene`
- `number.wohnzimmer_controller_livingroom_effect_intensity`
- `number.wohnzimmer_controller_livingroom_effect_speed`

### Rail Monitoring
- `sensor.wohnzimmer_controller_rail_12v_voltage`
- `sensor.wohnzimmer_controller_rail_12v_current`
- `sensor.wohnzimmer_controller_rail_12v_power`
- `sensor.wohnzimmer_controller_rail_5v_voltage`
- `sensor.wohnzimmer_controller_rail_5v_current`
- `sensor.wohnzimmer_controller_rail_5v_power`

### Klima / Lüfter
- `sensor.wohnzimmer_controller_av_receiver_temperature`
- `sensor.wohnzimmer_controller_cabinet_temperature`
- `sensor.wohnzimmer_controller_livingroom_temperature`
- `sensor.wohnzimmer_controller_livingroom_humidity`
- `sensor.wohnzimmer_controller_av_fan_rpm`

## Firmware flashen

1. `secrets.yaml` mit `wohnzimmer_api_key` und `wohnzimmer_ota_password` anlegen.
2. `livingroom.yaml` flashen:
   - per USB: `esphome run livingroom.yaml --device COMx`
   - per OTA: `esphome run livingroom.yaml --device <IP>`
3. Danach das Gerät in Home Assistant unter Einstellungen → Geräte & Dienste →
   ESPHome hinzufügen (Host = IP, Port 6053, Key aus `secrets.yaml`).

## Zentrale Konfiguration (YAML)

Alles ist versioniert im Repo unter `home-assistant/`:

- `configuration-snippet.yaml` → Inhalt in `/config/configuration.yaml` übernehmen
  (aktiviert `packages` und das YAML-Dashboard).
- `packages/livingroom_modes.yaml` → nach `/config/packages/` kopieren
  (Modi, Skripte, Automationen).
- `dashboards/livingroom.yaml` → nach `/config/dashboards/` kopieren
  (Dashboard inkl. TV-Lift-Slider).

Nach dem Kopieren: Entwicklerwerkzeuge → YAML → „Alle YAML-Konfigurationen"
neu laden bzw. einmal neu starten.

## Zweck des Mocks

Der Mock ist absichtlich hardwarefrei:
- keine LED-Pins
- kein Motor
- keine INA226
- keine Sensoren
- kein Risiko

Damit kannst du Home Assistant, Dashboard, Entity-Namen, Szenen und Automationen
einrichten, bevor die Hauptplatine fertig ist. Beim späteren Umbau auf echte
Hardware (`TODO HARDWARE`-Marker in `livingroom.yaml`) bleiben die Entity-IDs
identisch, deine HA-Konfiguration gilt also 1:1 weiter.
