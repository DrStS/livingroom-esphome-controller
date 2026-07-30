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

## Deploying the configuration

Do not copy files by hand. The whole Home Assistant configuration is versioned
under `home-assistant/config/` and rolled out over SSH:

```bash
python tools/ha.py check                # verify access first
python tools/ha.py setup --esphome      # deploy everything, validate, restart
```

That writes `configuration.yaml`, `automations.yaml`, `scripts.yaml`,
`scenes.yaml` and `dashboards/livingroom.yaml`, mirrors the ESPHome
configuration to `/config/esphome/`, has Home Assistant validate the result,
restarts it, and verifies that every dashboard entity exists.

Full walkthrough, including installing the SSH add-on and creating the
automation user and token: `docs/home-assistant-pipeline.md`.

There is no Home Assistant package anymore: the old room-mode package (helper
plus scripts) was deleted, and the scene select on the ESP covers the same job
without depending on Home Assistant.

## Verify entity IDs

After the device is added, run:

```bash
python tools/dump_entity_ids.py
```

It reads the entity IDs straight from the controller, so dashboards never end up
referencing entities that no longer exist. See `docs/entity-map.md` for the
current list.
