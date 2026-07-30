import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient

DOMAIN_BY_INFO = {
    "SensorInfo": "sensor",
    "TextSensorInfo": "sensor",
    "BinarySensorInfo": "binary_sensor",
    "SwitchInfo": "switch",
    "ButtonInfo": "button",
    "SelectInfo": "select",
    "NumberInfo": "number",
    "CoverInfo": "cover",
    "LightInfo": "light",
}


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Entity Dump",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        rows = []
        for entity in entities:
            kind = type(entity).__name__
            domain = DOMAIN_BY_INFO.get(kind)
            if domain is None:
                continue
            object_id = getattr(entity, "object_id", "")
            category = getattr(entity, "entity_category", "")
            rows.append((domain, f"{domain}.wohnzimmer_controller_{object_id}",
                         entity.name, str(category)))
        for row in sorted(rows):
            print(f"{row[1]:70} | {row[2]:32} | cat={row[3]}")
    finally:
        await client.disconnect()


asyncio.run(main())