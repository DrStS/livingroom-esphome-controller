"""Beweist, dass Referenz und Position einen Neustart ueberstehen.

Ablauf: Zustand lesen -> Referenz setzen -> Zustand lesen -> Controller neu
starten -> Zustand erneut lesen. Bewegt keine Hardware; "Lift Referenz" setzt
nur den aktuellen Encoderstand als Nullpunkt.

Aufruf aus dem Projektwurzelverzeichnis:
    python tools/test_persistence.py
"""

import asyncio
from pathlib import Path

import yaml
from aioesphomeapi import APIClient

DEVICE_HOST = "192.168.1.12"
WATCH = (
    "Lift Zustand",
    "Lift Referenz",
    "TV Lift Position Pulses",
    "TV Lift Position Percent",
    "TV Lift Position mm",
)


async def connect() -> APIClient:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient(DEVICE_HOST, 6053, None,
                       client_info="Kiro Persistence Test",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    return client


async def snapshot(client: APIClient, label: str) -> dict[str, object]:
    """Liest die interessanten Entitaeten einmalig ueber Zustandsmeldungen."""
    entities, _ = await client.list_entities_services()
    by_key = {e.key: e.name for e in entities}
    result: dict[str, object] = {}
    done = asyncio.Event()

    def on_state(state) -> None:
        name = by_key.get(state.key)
        if name in WATCH:
            value = getattr(state, "state", None)
            result[name] = value
            if len(result) == len(WATCH):
                done.set()

    client.subscribe_states(on_state)
    try:
        await asyncio.wait_for(done.wait(), timeout=10)
    except asyncio.TimeoutError:
        pass

    print(f"--- {label} ---")
    for name in WATCH:
        print(f"  {name:26} = {result.get(name, '(keine Meldung)')}")
    print()
    return result


async def press_switch(client: APIClient, name: str, state: bool) -> None:
    entities, _ = await client.list_entities_services()
    for entity in entities:
        if entity.name == name and type(entity).__name__ == "SwitchInfo":
            client.switch_command(entity.key, state)
            print(f"Schalter '{name}' -> {'EIN' if state else 'AUS'}\n")
            return
    raise SystemExit(f"Schalter '{name}' nicht gefunden")


async def press_button(client: APIClient, name: str) -> None:
    entities, _ = await client.list_entities_services()
    for entity in entities:
        if entity.name == name and type(entity).__name__ == "ButtonInfo":
            client.button_command(entity.key)
            print(f"Button '{name}' ausgeloest\n")
            return
    raise SystemExit(f"Button '{name}' nicht gefunden")


async def main() -> None:
    client = await connect()
    try:
        await snapshot(client, "1. Vorher")
        await press_switch(client, "Lift Referenz", True)
        await asyncio.sleep(2)
        after_ref = await snapshot(client, "2. Nach dem Referenzieren")
        await press_button(client, "Controller Restart")
    finally:
        await client.disconnect()

    print("Warte auf den Neustart ...\n")
    await asyncio.sleep(20)

    client = await connect()
    try:
        after_boot = await snapshot(client, "3. Nach dem Neustart")
    finally:
        await client.disconnect()

    ok = (str(after_boot.get("Lift Referenz")) == str(after_ref.get("Lift Referenz"))
          and str(after_boot.get("Lift Zustand")) != "UNREFERENCED")
    print("ERGEBNIS: " + ("Referenz hat den Neustart ueberstanden"
                          if ok else "Referenz ist verloren gegangen"))


asyncio.run(main())
