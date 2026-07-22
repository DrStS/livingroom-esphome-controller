# Entity Map

Actual entity IDs of the ESPHome device. Home Assistant prepends the device
name (`wohnzimmer-controller`) to every ESPHome entity, so all controller IDs
carry the `wohnzimmer_controller_` prefix.

## Whole-room mode (Home Assistant helpers)

| Entity | Type | Purpose |
|---|---|---|
| `input_select.livingroom_mode` | Helper | Whole-room mode selector |
| `script.livingroom_set_mode` | Script | Applies a complete room mode |

## ESP furniture controller

| Entity | Type | Hardware later |
|---|---|---|
| `cover.wohnzimmer_controller_tv_lift` | Cover | LiftController + motor driver + encoder |
| `sensor.wohnzimmer_controller_tv_lift_position_percent` | Sensor | Encoder / persisted position |
| `sensor.wohnzimmer_controller_tv_lift_position_pulses` | Sensor | Encoder |
| `binary_sensor.wohnzimmer_controller_tv_lift_homed` | Binary Sensor | Endstop / homing state |
| `binary_sensor.wohnzimmer_controller_tv_lift_fault` | Binary Sensor | LiftController fault state |
| `button.wohnzimmer_controller_tv_lift_reference` | Button | Homing/reference run |
| `button.wohnzimmer_controller_tv_lift_clear_fault` | Button | Clears fault |
| `light.wohnzimmer_controller_sideboard_ambient` | RGBW Light | SK6812 RGBW, 112 px, GPIO45 |
| `light.wohnzimmer_controller_cabinet_glass_edge` | RGBW Light | SK6812 RGBW, 103 px, GPIO46 |
| `select.wohnzimmer_controller_livingroom_light_scene` | Select | ESP LED mode |
| `number.wohnzimmer_controller_livingroom_effect_intensity` | Number | ESP LED scene intensity |
| `number.wohnzimmer_controller_livingroom_effect_speed` | Number | ESP LED scene speed |
| `sensor.wohnzimmer_controller_rail_12v_voltage` | Sensor | INA226 12 V rail |
| `sensor.wohnzimmer_controller_rail_12v_current` | Sensor | INA226 12 V rail |
| `sensor.wohnzimmer_controller_rail_12v_power` | Sensor | INA226 12 V rail |
| `sensor.wohnzimmer_controller_rail_5v_voltage` | Sensor | INA226 5 V rail |
| `sensor.wohnzimmer_controller_rail_5v_current` | Sensor | INA226 5 V rail |
| `sensor.wohnzimmer_controller_rail_5v_power` | Sensor | INA226 5 V rail |
| `sensor.wohnzimmer_controller_av_receiver_temperature` | Sensor | DS18B20 |
| `sensor.wohnzimmer_controller_cabinet_temperature` | Sensor | DS18B20 |
| `sensor.wohnzimmer_controller_livingroom_temperature` | Sensor | SHT45 |
| `sensor.wohnzimmer_controller_livingroom_humidity` | Sensor | SHT45 |
| `sensor.wohnzimmer_controller_av_fan_rpm` | Sensor | Fan tacho |

> Note: entity IDs are currently served by the hardware-free mock in
> `livingroom.yaml`. They stay identical when subsystems are switched to real
> hardware, so Home Assistant dashboards/automations remain valid.
