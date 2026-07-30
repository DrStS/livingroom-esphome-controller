import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Scene Check",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        for entity in entities:
            kind = type(entity).__name__
            if kind == "SelectInfo":
                print(f"Select '{entity.name}': {list(entity.options)}")
            if kind == "LightInfo":
                print(f"Light  '{entity.name}': {list(entity.effects)}")
    finally:
        await client.disconnect()


asyncio.run(main())