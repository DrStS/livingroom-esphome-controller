# Entity Map

Actual entity IDs of the ESPHome device, read back from the running controller
over the ESPHome API. Home Assistant prepends the device name
(`wohnzimmer-controller`) to every ESPHome entity, so all controller IDs carry
the `wohnzimmer_controller_` prefix.

There are no Home Assistant helper entities. The former room-mode package
(helper plus scripts) is gone; the scene select on the ESP does the same job and
keeps working without Home Assistant.

## TV lift

| Entity | Type | Notes |
|---|---|---|
| `cover.wohnzimmer_controller_tv_lift` | Cover | Arrows move manually, the position slider drives to a target (only once referenced) |
| `switch.wohnzimmer_controller_lift_referenz` | Switch (diagnostic) | ON = lift stands on the lower end position, sets position 0. OFF discards the reference |
| `sensor.wohnzimmer_controller_tv_lift_lage` | Text Sensor | `Unten`, `Oben`, `Zwischenlage xx %`, `Faehrt hoch`, `Faehrt runter`, `Nicht referenziert` |
| `sensor.wohnzimmer_controller_tv_lift_position_percent` | Sensor | 0–100 % of the 94500-count travel |
| `sensor.wohnzimmer_controller_tv_lift_position_mm` | Sensor | mm (144 counts/mm) |
| `sensor.wohnzimmer_controller_tv_lift_position_pulses` | Sensor (diagnostic) | Lift position in encoder counts |
| `sensor.wohnzimmer_controller_motor_encoder_position` | Sensor (diagnostic) | Raw PCNT counter value |
| `sensor.wohnzimmer_controller_lift_geschwindigkeit` | Sensor (diagnostic) | Measured encoder speed in counts/s |
| `sensor.wohnzimmer_controller_lift_zustand` | Text Sensor (diagnostic) | `IDLE`, `MANUAL`, `MOVING`, `REACHED`, `STALL`, `TIMEOUT`, `LIMIT`, `FAULT`, `UNREFERENCED` |
| `number.wohnzimmer_controller_lift_max_geschwindigkeit` | Number (diagnostic/config) | Cruise speed in counts/s, default 1500 |

## Light

| Entity | Type | Hardware |
|---|---|---|
| `light.wohnzimmer_controller_sideboard` | RGBW Light | SK6812 RGBW, 111 px, GPIO45 (SPI-DMA) |
| `light.wohnzimmer_controller_cabinet` | RGBW Light | SK6812 RGBW, 102 px, GPIO46 (RMT-DMA) |
| `select.wohnzimmer_controller_livingroom_light_scene` | Select | Room scene, applies effects on both strips |
| `number.wohnzimmer_controller_livingroom_effect_intensity` | Number | Scene intensity |

Effect list per strip (verified on the device):

| Strip | Effects |
|---|---|
| Sideboard | `None`, `Fireplace`, `Cinema`, `Fireplace Palette (Kalibrierung)`, `Fireworks` |
| Cabinet | `None`, `Fireplace`, `Fireplace Palette (Kalibrierung)`, `Museum`, `Fireworks` |

`Museum` (formerly "Cabinet Museum") exists on the cabinet only. `Cinema` exists
on the sideboard only.

Scene options and what they set:

| Scene | Sideboard | Cabinet |
|---|---|---|
| `Manual` | untouched | untouched |
| `Aus` | off | off |
| `Kaminfeuer` | Fireplace | Fireplace |
| `Vitrine` | off | Museum |
| `Kaminfeuer + Vitrine` | Fireplace | Museum |
| `Cinema` | Cinema | Museum |
| `Feuerwerk` | Fireworks | Fireworks |

## Power rails (2× INA226)

| Entity | Type |
|---|---|
| `sensor.wohnzimmer_controller_rail_12v_voltage` | Sensor |
| `sensor.wohnzimmer_controller_rail_12v_current` | Sensor |
| `sensor.wohnzimmer_controller_rail_12v_power` | Sensor |
| `sensor.wohnzimmer_controller_rail_12v_shunt_voltage` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_rail_5v_voltage` | Sensor |
| `sensor.wohnzimmer_controller_rail_5v_current` | Sensor |
| `sensor.wohnzimmer_controller_rail_5v_power` | Sensor |
| `sensor.wohnzimmer_controller_rail_5v_shunt_voltage` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_rail_12v_energy_daily` | Sensor (Wh/day) |
| `sensor.wohnzimmer_controller_rail_5v_energy_daily` | Sensor (Wh/day) |
| `sensor.wohnzimmer_controller_rail_total_power` | Sensor (12 V + 5 V) |

## Climate and fan

| Entity | Type | Hardware |
|---|---|---|
| `sensor.wohnzimmer_controller_av_receiver_temperature` | Sensor | DS18B20 |
| `sensor.wohnzimmer_controller_cabinet_temperature` | Sensor | DS18B20 |
| `sensor.wohnzimmer_controller_livingroom_temperature` | Sensor | DHT22 |
| `sensor.wohnzimmer_controller_livingroom_humidity` | Sensor | DHT22 |
| `sensor.wohnzimmer_controller_av_fan_rpm` | Sensor | Fan tacho, GPIO38 |
| `sensor.wohnzimmer_controller_av_fan_duty` | Sensor (diagnostic) | PWM duty set by the temperature curve |

## System

| Entity | Type |
|---|---|
| `sensor.wohnzimmer_controller_uptime` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_esp_temperature` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_heap_free` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_loop_time` | Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_ip_address` | Text Sensor (diagnostic) |
| `sensor.wohnzimmer_controller_mac_address` | Text Sensor (diagnostic) |
| `binary_sensor.wohnzimmer_controller_api_connected` | Binary Sensor (diagnostic) |
| `binary_sensor.wohnzimmer_controller_rail_12v_overcurrent` | Binary Sensor (problem) |
| `binary_sensor.wohnzimmer_controller_rail_5v_overcurrent` | Binary Sensor (problem) |
| `button.wohnzimmer_controller_controller_restart` | Button |

## Keeping this list honest

`tools/dump_entity_ids.py` prints every entity with its Home Assistant entity ID
straight from the device. Run it before editing dashboards so no stale IDs creep
back in.
