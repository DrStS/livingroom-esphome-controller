# Home Assistant Mock Setup

## Was du in Home Assistant siehst

Nach Integration des Mock-ESPHome-Geräts erscheinen diese Entities:

### Lift
- `cover.tv_lift`
- `sensor.tv_lift_position_percent`
- `sensor.tv_lift_position_pulses`
- `binary_sensor.tv_lift_homed`
- `binary_sensor.tv_lift_fault`
- `button.tv_lift_reference`
- `button.tv_lift_clear_fault`
- `button.tv_lift_simulate_fault`

### Licht
- `light.sideboard_ambient`
- `light.cabinet_glass_edge`
- `light.cabinet_inner_diffuser`
- `select.livingroom_light_scene`
- `number.livingroom_effect_intensity`
- `number.livingroom_effect_speed`

### Rail Monitoring
- `sensor.12v_rail_voltage`
- `sensor.12v_rail_current`
- `sensor.12v_rail_power`
- `sensor.5v_rail_voltage`
- `sensor.5v_rail_current`
- `sensor.5v_rail_power`

### Klima / Lüfter
- `sensor.av_receiver_temperature`
- `sensor.cabinet_temperature`
- `sensor.livingroom_temperature`
- `sensor.livingroom_humidity`
- `sensor.av_fan_rpm`

## Mock flashen

1. ESPHome Add-on in Home Assistant installieren.
2. `livingroom_mock.yaml` als neues ESPHome-Gerät anlegen.
3. `secrets.yaml` mit `wohnzimmer_api_key` und `wohnzimmer_ota_password` anlegen.
4. Einmal per USB flashen oder über das ESPHome Dashboard kompilieren.
5. Danach erscheint das Gerät automatisch als ESPHome-Integration.

## Dashboard

Die Datei `home-assistant/lovelace-livingroom-dashboard.yaml` ist ein einfacher Lovelace-Startpunkt.

In Home Assistant:
1. Dashboard öffnen.
2. Drei-Punkte-Menü.
3. Raw configuration editor.
4. YAML aus der Datei einfügen oder als eigenes Dashboard übernehmen.

## Zweck des Mocks

Der Mock ist absichtlich hardwarefrei:
- keine LED-Pins
- kein Motor
- keine INA226
- keine Sensoren
- kein Risiko

Damit kannst du Home Assistant, Dashboard, Entity-Namen, Szenen und Automationen einrichten, bevor die Hauptplatine fertig ist.
