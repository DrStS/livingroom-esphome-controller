import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Lift Log Watch",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        names = {entity.key: entity.name for entity in entities}
        watched = {"Lift Zustand", "Motor Encoder Position", "Loop Time"}

        def on_log(entry) -> None:
            text = entry.message.decode("utf-8", "replace") if isinstance(entry.message, bytes) else str(entry.message)
            if "lift" in text.lower() or "motor" in text.lower():
                print(f"LOG  | {text.strip()}", flush=True)

        def on_state(state) -> None:
            name = names.get(state.key, "")
            if name in watched and hasattr(state, "state"):
                print(f"STATE| {name} = {state.state}", flush=True)

        client.subscribe_logs(on_log)
        client.subscribe_states(on_state)
        print("MESSUNG LAEUFT 25 SEKUNDEN -- jetzt Hoch oder Runter druecken.", flush=True)
        await asyncio.sleep(25)
        print("MESSUNG BEENDET.", flush=True)
    finally:
        await client.disconnect()


asyncio.run(main())