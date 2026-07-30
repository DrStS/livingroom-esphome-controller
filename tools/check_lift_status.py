import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Lift Ready Check",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        wanted = {}
        for entity in entities:
            kind = type(entity).__name__
            if entity.name in {"Lift Referenz", "Lift Zustand", "TV Lift",
                               "TV Lift Position Pulses", "TV Lift Position Percent",
                               "TV Lift Position mm", "Lift Geschwindigkeit"}:
                wanted[entity.key] = (kind, entity.name)

        found = {}

        def on_state(state) -> None:
            if state.key in wanted:
                kind, name = wanted[state.key]
                if kind == "CoverInfo":
                    found[name] = f"position={getattr(state, 'position', None)}"
                else:
                    found[name] = getattr(state, "state", None)

        client.subscribe_states(on_state)
        await asyncio.sleep(3)
        for name in sorted(found):
            print(f"{name:28} = {found[name]}")
    finally:
        await client.disconnect()


asyncio.run(main())