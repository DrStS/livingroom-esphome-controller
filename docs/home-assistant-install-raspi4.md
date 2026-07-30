# Home Assistant on Raspberry Pi 4

## Recommended path

Use **Home Assistant OS** on the Raspberry Pi 4.

Why:
- easiest installation
- Supervisor included
- ESPHome add-on available
- good for a dedicated home automation controller

## Hardware

- Raspberry Pi 4
- official or good 5 V USB-C power supply
- microSD card or, better, USB SSD
- Ethernet connection
- another computer for flashing the image

## Install outline

1. Install Raspberry Pi Imager on your computer.
2. Select:
   - Other specific-purpose OS
   - Home automation
   - Home Assistant
   - Home Assistant OS for Raspberry Pi 4
3. Write the image to SD/SSD.
4. Boot the Raspberry Pi 4 via Ethernet.
5. Open:
   - `http://homeassistant.local:8123`
   - or the IP address shown by your router.
6. Create the Home Assistant user.
7. Install the ESPHome Device Builder add-on.
8. Import `livingroom.yaml` together with `config/` and `components/`.
9. Flash the ESP32-S3.
10. Add the ESPHome device under Settings → Devices & services
    (host = device IP, port 6053, key from `secrets.yaml`).

There is no mock firmware anymore. `livingroom.yaml` is the only firmware and it
drives real hardware.

## Files to copy

ESPHome configuration:

```text
/config/esphome/livingroom.yaml
/config/esphome/config/pins.yaml
/config/esphome/config/hardware.yaml
/config/esphome/config/effects_sideboard.yaml
/config/esphome/config/effects_cabinet.yaml
/config/esphome/components/...
/config/esphome/secrets.yaml
```

Dashboard:

```text
/config/dashboards/livingroom.yaml
```

`home-assistant/dashboards/livingroom.yaml` has three views: Uebersicht, Verlauf,
Diagnose. If you prefer a single compact view, use
`home-assistant/lovelace-livingroom-dashboard.yaml` instead.

To register the YAML dashboard, take the `lovelace:` block from:

```text
home-assistant/configuration-snippet.yaml
```

and add it to:

```text
/config/configuration.yaml
```

Nothing else needs to be copied. There is no Home Assistant package anymore: the
old room-mode package (helper plus scripts) was deleted, and the scene select on
the ESP covers the same job without depending on Home Assistant. Ignore the
leftover `homeassistant: packages:` line in the snippet.

Then restart Home Assistant.

## Verify entity IDs

After the device is added, run:

```bash
python tools/dump_entity_ids.py
```

It reads the entity IDs straight from the controller, so dashboards never end up
referencing entities that no longer exist. See `docs/entity-map.md` for the
current list.
