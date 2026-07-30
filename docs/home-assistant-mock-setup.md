# Home Assistant einrichten

Historischer Dateiname: hier stand frueher das hardwarefreie Mock-Setup. Einen
Mock gibt es nicht mehr, `livingroom_mock.yaml` ist entfallen. Es existiert nur
noch die Produktionsfirmware `livingroom.yaml` (Geraetename
`wohnzimmer-controller`) mit echter Hardware.

Home Assistant haengt den Geraetenamen als Praefix an die IDs an, daher tragen
alle Controller-Entities das Praefix `wohnzimmer_controller_`.

## Firmware flashen

1. `secrets.yaml` mit `wohnzimmer_api_key` und `wohnzimmer_ota_password` anlegen
   (Vorlage: `secrets.example.yaml`).
2. `livingroom.yaml` flashen:
   - per USB: `esphome run livingroom.yaml --device COMx`
   - per OTA: `esphome run livingroom.yaml --device 192.168.1.12`
3. Geraet in Home Assistant unter Einstellungen → Geraete & Dienste → ESPHome
   hinzufuegen (Host = IP, Port 6053, Key aus `secrets.yaml`).

Beim OTA-Start stoppt die Firmware den Lift, bevor das Update beginnt.

## Was du in Home Assistant siehst

Vollstaendige Liste mit Typen: `docs/entity-map.md`. Kurzfassung:

### Lift
- `cover.wohnzimmer_controller_tv_lift`
- `switch.wohnzimmer_controller_lift_referenz` (EIN = untere Endlage erreicht,
  setzt Position 0; AUS verwirft die Referenz)
- `sensor.wohnzimmer_controller_tv_lift_lage`
- `sensor.wohnzimmer_controller_tv_lift_position_percent`
- `sensor.wohnzimmer_controller_tv_lift_position_mm`
- `sensor.wohnzimmer_controller_tv_lift_position_pulses` (Diagnose)
- `sensor.wohnzimmer_controller_motor_encoder_position` (Diagnose)
- `sensor.wohnzimmer_controller_lift_geschwindigkeit` (Diagnose)
- `sensor.wohnzimmer_controller_lift_zustand` (Diagnose)
- `number.wohnzimmer_controller_lift_max_geschwindigkeit` (Diagnose/Config)

### Licht
- `light.wohnzimmer_controller_sideboard` (111 px)
- `light.wohnzimmer_controller_cabinet` (102 px)
- `select.wohnzimmer_controller_livingroom_light_scene`
- `number.wohnzimmer_controller_livingroom_effect_intensity`

### Schienen
- `sensor.wohnzimmer_controller_rail_12v_voltage` / `_current` / `_power` /
  `_shunt_voltage`
- `sensor.wohnzimmer_controller_rail_5v_voltage` / `_current` / `_power` /
  `_shunt_voltage`
- `sensor.wohnzimmer_controller_rail_12v_energy_daily`
- `sensor.wohnzimmer_controller_rail_5v_energy_daily`
- `sensor.wohnzimmer_controller_rail_total_power`

### Klima / Luefter
- `sensor.wohnzimmer_controller_av_receiver_temperature`
- `sensor.wohnzimmer_controller_cabinet_temperature`
- `sensor.wohnzimmer_controller_livingroom_temperature`
- `sensor.wohnzimmer_controller_livingroom_humidity`
- `sensor.wohnzimmer_controller_av_fan_rpm`
- `sensor.wohnzimmer_controller_av_fan_duty` (Diagnose)

### System
- `sensor.wohnzimmer_controller_uptime`, `_esp_temperature`, `_heap_free`,
  `_loop_time`, `_ip_address`, `_mac_address` (alle Diagnose)
- `binary_sensor.wohnzimmer_controller_api_connected`
- `binary_sensor.wohnzimmer_controller_rail_12v_overcurrent`
- `binary_sensor.wohnzimmer_controller_rail_5v_overcurrent`
- `button.wohnzimmer_controller_controller_restart`

## Szenen

Die Szenenauswahl laeuft komplett auf dem ESP, nicht in Home Assistant:

| Szene | Sideboard | Cabinet |
|---|---|---|
| `Manual` | kein Eingriff | kein Eingriff |
| `Aus` | aus | aus |
| `Kaminfeuer` | Fireplace | Fireplace |
| `Vitrine` | aus | Museum |
| `Kaminfeuer + Vitrine` | Fireplace | Museum |
| `Cinema` | Cinema | Museum |
| `Feuerwerk` | Fireworks | Fireworks |

Szene und Intensitaet werden per `restore_value` ueber Neustart und
Spannungsausfall gehalten, die Einzelzustaende der Lichter per `restore_mode`.

## Dashboards

Im Repo unter `home-assistant/`. Es gibt kein Package mehr, das kopiert werden
muesste: das frueher hier beschriebene Modi-Package ist geloescht, der
packages-Mechanismus wird nicht mehr gebraucht.

- `dashboards/livingroom.yaml` → nach `/config/dashboards/` kopieren. Drei
  Ansichten: Uebersicht (Bedienung und aktuelle Werte), Verlauf (Graphen),
  Diagnose (Referenzieren, Reglerzustand, Systemwerte).
- `lovelace-livingroom-dashboard.yaml` → kompakte Einzelansicht als Alternative,
  wenn du keine drei Ansichten willst.
- `configuration-snippet.yaml` → daraus nur den `lovelace:`-Block in
  `/config/configuration.yaml` uebernehmen, der das YAML-Dashboard registriert.
  Die `homeassistant: packages:`-Zeile in der Datei ist ein Ueberrest des
  geloeschten Packages und wird nicht mehr gebraucht (offen: die Zeile ist noch
  nicht aus dem Snippet entfernt).

Nach dem Kopieren: Entwicklerwerkzeuge → YAML neu laden bzw. einmal neu starten.

## Entity-IDs pruefen

Vor jeder Dashboard-Aenderung `python tools/dump_entity_ids.py` laufen lassen.
Das Skript fragt die IDs direkt am Geraet ab, damit keine veralteten Entities in
die Konfiguration zurueckwandern.
