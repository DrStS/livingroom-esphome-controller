import asyncio
import math
from pathlib import Path

import yaml
from aioesphomeapi import APIClient


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Sensor Check",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        wanted = {
            entity.key: entity for entity in entities
            if type(entity).__name__ in {"SensorInfo", "BinarySensorInfo", "TextSensorInfo"}
        }
        states = {}

        def on_state(state) -> None:
            if state.key in wanted and hasattr(state, "state"):
                states[state.key] = state

        client.subscribe_states(on_state)
        await asyncio.sleep(8)
        print(f"SENSOREN GEFUNDEN: {len(wanted)}")
        for key, entity in sorted(wanted.items(), key=lambda item: item[1].name.lower()):
            state = states.get(key)
            if state is None or getattr(state, "missing_state", False):
                value = "KEIN WERT"
            else:
                raw = state.state
                value = "NAN/UNBEKANNT" if isinstance(raw, float) and math.isnan(raw) else str(raw)
                unit = getattr(entity, "unit_of_measurement", "")
                if unit and value not in {"NAN/UNBEKANNT", "KEIN WERT"}:
                    value += f" {unit}"
            print(f"{type(entity).__name__:16} | {entity.name:32} | {value}")
    finally:
        await client.disconnect()


asyncio.run(main())