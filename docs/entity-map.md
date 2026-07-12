# Entity Map

Expected entity IDs after first mock install. Home Assistant may add a suffix if a duplicate already exists.

## Whole-room mode

| Entity | Type | Purpose |
|---|---|---|
| `input_select.livingroom_mode` | Helper | Whole-room mode selector |
| `script.livingroom_set_mode` | Script | Applies a complete room mode |

## ESP furniture controller

| Entity | Type | Hardware later |
|---|---|---|
| `cover.tv_lift` | Cover | LiftController + motor driver + encoder |
| `sensor.tv_lift_position_percent` | Sensor | Encoder / persisted position |
| `sensor.tv_lift_position_pulses` | Sensor | Encoder |
| `binary_sensor.tv_lift_homed` | Binary Sensor | Endstop / homing state |
| `binary_sensor.tv_lift_fault` | Binary Sensor | LiftController fault state |
| `button.tv_lift_reference` | Button | Homing/reference run |
| `button.tv_lift_clear_fault` | Button | Clears fault |
| `button.tv_lift_simulate_fault` | Button | Mock only |
| `light.sideboard_ambient` | RGBW Light | LED DATA through 74AHCT125 |
| `light.cabinet_glass_edge` | RGBW Light | LED DATA through 74AHCT125 |
| `light.cabinet_inner_diffuser` | RGBW Light | LED DATA through 74AHCT125 |
| `select.livingroom_light_scene` | Select | ESP LED mode |
| `number.livingroom_effect_intensity` | Number | ESP LED scene intensity |
| `number.livingroom_effect_speed` | Number | ESP LED scene speed |
| `sensor.rail_12v_voltage` | Sensor | INA226 12 V rail |
| `sensor.rail_12v_current` | Sensor | INA226 12 V rail |
| `sensor.rail_12v_power` | Sensor | INA226 12 V rail |
| `sensor.rail_5v_voltage` | Sensor | INA226 5 V rail |
| `sensor.rail_5v_current` | Sensor | INA226 5 V rail |
| `sensor.rail_5v_power` | Sensor | INA226 5 V rail |
| `sensor.av_receiver_temperature` | Sensor | DS18B20 |
| `sensor.cabinet_temperature` | Sensor | DS18B20 |
| `sensor.livingroom_temperature` | Sensor | SHT45 |
| `sensor.livingroom_humidity` | Sensor | SHT45 |
| `sensor.av_fan_rpm` | Sensor | Fan tacho |
