# Living Room ESPHome Controller

This project replaces the old Arduino/OpenHAB serial furniture controller with an ESP32-S3 Ethernet/PoE ESPHome controller.

The ESP owns all local real-time behavior, so the room keeps working even without Home Assistant:
- TV lift closed-loop position control (own FreeRTOS task, 1 ms, trapezoid profile plus PI speed control)
- lift safety: soft limits 0..94500 counts, stall cutoff, timeout, heartbeat watchdogs, NVS position/reference persistence
- LED scene engine on two SK6812 RGBW strips (111 px sideboard, 102 px cabinet)
- temperature-driven fan control
- power rail monitoring (2× INA226)
- room and cabinet sensors

Home Assistant is the user interface and the history/graph store. Room scenes are
selected on the ESP via `select.wohnzimmer_controller_livingroom_light_scene`:
Manual, Aus, Kaminfeuer, Vitrine, Kaminfeuer + Vitrine, Cinema, Feuerwerk.

The earlier Home Assistant room-mode package (helper plus scripts) was deleted;
the ESP scene select replaces it.

## Main files

```text
livingroom.yaml
  The firmware. Real hardware, no mock.

config/pins.yaml
  Central GPIO mapping.

config/hardware.yaml
  Central hardware parameters: LED counts, cabinet zone map, cinema_* effect
  parameters, INA226 addresses and shunts, lift travel and drivetrain ratios.

config/effects_sideboard.yaml / config/effects_cabinet.yaml
  Per-strip effect lists.

components/
  ESPHome external components and C++ cores:
  lift_motor (closed-loop lift), pcnt_quadrature (hardware quadrature encoder),
  fireplace_effect, spi_clockless_led, serialized_rmt_led.


tools/
  Diagnostic scripts that talk to the running device over the ESPHome API:
  check_entity_states.py, check_lift_status.py, dump_entity_ids.py,
  check_light_effects.py, watch_lift_logs.py, encoder_diagnose.py,
  verify_dashboard_entities.py, test_persistence.py.
  See tools/README.md. encoder_diagnose.py moves the lift.

tools/ha.py
  Home Assistant pipeline: deploy the whole HA setup over SSH, validate,
  reload, restart, and verify entities over the REST API. Replaces copying
  YAML by hand. See docs/home-assistant-pipeline.md.

bringup/
  Archived encoder/motor bring-up firmwares, kept for traceability only.
  These are complete firmwares that replace the production one and drive the
  motor outputs without encoder supervision. Read bringup/README.md before use.

home-assistant/config/
  The Home Assistant configuration, source of truth: configuration.yaml,
  automations/scripts/scenes, and dashboards/livingroom.yaml (three views:
  Uebersicht, Verlauf, Diagnose). Deployed by tools/ha.py, never edited on
  the host. See home-assistant/README.md.

home-assistant/deploy-manifest.yaml
  Declares which file goes where on the host.

docs/
  Setup and design documentation. Start with docs/home-assistant-pipeline.md
  for the HA side, docs/entity-map.md and docs/hardware-pinmap.md for the
  device side.
```

## Deploying Home Assistant

The HA setup is versioned here and rolled out with one command instead of
pasting YAML into the UI:

```text
python tools/ha.py check      verify access (network, SSH, API token)
python tools/ha.py setup      full first-time rollout
python tools/ha.py deploy     push dashboards (everyday case)
python tools/ha.py verify     check dashboards against the HA registry
```

Access data lives in `secrets.yaml` (`ha_host`, `ha_token`, `ha_ssh_key`);
see `secrets.example.yaml`. Full walkthrough including a fresh HA OS install:
`docs/home-assistant-pipeline.md`.

## Verified drivetrain numbers

| Item | Value |
|---|---|
| Encoder | Hall, on the motor shaft, 12 counts per motor revolution (3 signal cycles per channel, x4) |
| Gearbox | 48:1 |
| Screw lead | 4 mm |
| Resolution | 576 counts per screw revolution → 144 counts/mm → 1 count = 6.94 µm |
| Travel | 94500 counts = 656.25 mm |
| Cruise speed | 1500 counts/s (~10.4 mm/s, full travel about 63 s) |

The datasheet line "6 signals per rotation" counts edges, not cycles. Confirmed
by comparing the PCNT delta (496) against the summed A+B edges (494).

## Why pins are in YAML

The ESP does not parse YAML at runtime. ESPHome uses YAML at build time to generate C++ firmware.

The clean split is:

```text
YAML:
- GPIO mapping
- device addresses
- pixel counts
- travel limits, gear ratio, motion profile
- board variants

C++20:
- lift logic
- safety
- motion profile execution
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
