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
8. Import `livingroom_mock.yaml`.
9. Flash the ESP32-S3 once.
10. Add the ESPHome device under Settings → Devices & services.

## Files to copy later

Copy this project content into Home Assistant:

```text
/config/esphome/livingroom_mock.yaml
/config/esphome/livingroom.yaml
/config/esphome/components/living_room/...
/config/packages/livingroom_modes.yaml
/config/dashboards/livingroom.yaml
```

Add the content from:

```text
home-assistant/configuration-snippet.yaml
```

to:

```text
/config/configuration.yaml
```

Then restart Home Assistant.
