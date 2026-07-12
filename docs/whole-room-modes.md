# Whole-Room Modes

## Architecture

```text
Home Assistant whole-room mode
        ↓
sets ESPHome entities
        ↓
ESP32 executes local LED/lift/safety logic
```

The ESP32 should not know about the whole room. It controls the furniture:
- TV lift
- LED zones
- fan
- sensors
- power rails
- safety/faults

Home Assistant owns room-level intent:
- Romantic Campfire
- Cinema
- Gaming
- Maintenance
- TV Lift Show
- Off

## Mode mapping

| Whole-room mode | ESP LED scene | Lift | Intensity | Speed |
|---|---:|---:|---:|---:|
| Off | Manual | unchanged | unchanged | unchanged |
| Manual | Manual | unchanged | unchanged | unchanged |
| Romantic Campfire | Fireplace | closed | 55 | 35 |
| Cinema | Cinema | open | 25 | 10 |
| Gaming | Aurora | open | 70 | 50 |
| Maintenance | Maintenance | unchanged | 100 | unchanged |
| TV Lift Show | Lift Show | open | 75 | 45 |

## Why this split is correct

Home Assistant is good at:
- room-level scenes
- UI
- automations
- connecting other devices

ESP32 is good at:
- fast LED frame generation
- deterministic lift state machine
- encoder/endstop safety
- local fallback behavior
