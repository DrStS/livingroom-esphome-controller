# Living Room ESPHome Controller

This project replaces the old Arduino/OpenHAB serial furniture controller with an ESP32-S3 Ethernet/PoE ESPHome controller.

The ESP32 owns the local real-time behavior:
- TV lift state machine
- encoder/endstop safety
- LED scene engine
- fan logic
- power rail monitoring
- sensors

Home Assistant owns the room-level intent:
- Romantic Campfire
- Cinema
- Gaming
- Maintenance
- TV Lift Show
- Off

## Main files

```text
livingroom_mock.yaml
  Hardware-free ESPHome mock for Home Assistant setup.

livingroom.yaml
  Real hardware firmware.

config/pins.yaml
  Central GPIO mapping.

config/hardware.yaml
  Central hardware parameters: LED counts, INA226 addresses, shunts, lift pulse limits.

components/living_room/
  ESPHome external component and C++20 controller core.

home-assistant/packages/livingroom_modes.yaml
  Whole-room mode helper, scripts, and automations.

home-assistant/dashboards/livingroom.yaml
  Built-in Lovelace dashboard.

docs/
  Setup and design documentation.
```

## First target

Install Home Assistant on the Raspberry Pi 4, install ESPHome Device Builder, flash `livingroom_mock.yaml`, and verify the full Home Assistant UI before connecting motor, LEDs, INA226 sensors, or endstops.

## Why there is a mock

The mock creates the same Home Assistant entities without any hardware risk:
- no motor outputs
- no LED outputs
- no INA226 dependency
- no DS18B20/SHT45 dependency
- simulated lift position
- simulated rails
- simulated fan RPM and temperatures

This lets us finish dashboards, mode scripts, and automations first.

## Why pins are in YAML

The ESP does not parse YAML at runtime. ESPHome uses YAML at build time to generate C++ firmware.

The clean split is:

```text
YAML:
- GPIO mapping
- device addresses
- pixel counts
- board variants

C++20:
- lift logic
- safety
- motion profile
- LED scene generation
- fan logic
```

That keeps the C++ core testable and board-independent.


## Public repository setup

Suggested repository:

```text
SickiBoat/livingroom-esphome-controller
```

This repository intentionally does not include a license file yet. Public on GitHub does not automatically mean open-source licensing. Add a license later only when the intended usage rights are clear.

## Push from a local checkout

```bash
git init
git branch -M main
git add .
git commit -m "feat: initial living room ESPHome controller"
git remote add origin git@github.com:SickiBoat/livingroom-esphome-controller.git
git push -u origin main
```

Do not commit `secrets.yaml`. Use `secrets.example.yaml` as the template.
