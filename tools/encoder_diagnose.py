import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient("192.168.1.12", 6053, None,
                       client_info="Kiro Encoder Diagnose",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        button = next(entity for entity in entities
                      if type(entity).__name__ == "ButtonInfo"
                      and entity.name == "Lift Encoder Diagnose")

        def on_log(entry) -> None:
            message = entry.message
            text = message.decode("utf-8", "replace") if isinstance(message, bytes) else str(message)
            print(text.strip(), flush=True)

        from aioesphomeapi import LogLevel

        client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_DEBUG)
        await asyncio.sleep(1)
        print(">>> Diagnose wird ausgeloest (Lift faehrt kurz aufwaerts)...", flush=True)
        client.button_command(button.key)
        await asyncio.sleep(8)
        print(">>> Ende.", flush=True)
    finally:
        await client.disconnect()


asyncio.run(main())