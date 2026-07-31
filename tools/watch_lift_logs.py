"""Zeitgestützter Lift-Fehlermitschnitt über ESPHome-API und TCP-Ports.

Bleibt aktiv, wenn die API abbricht, und schreibt alle Ereignisse mit Uhrzeit
in eine Datei. Zugangsdaten werden weder protokolliert noch ausgegeben.

Aufruf:
    python tools/watch_lift_logs.py
    python tools/watch_lift_logs.py --minutes 30
"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
from pathlib import Path

import yaml
from aioesphomeapi import APIClient

HOST = "192.168.1.12"
API_PORT = 6053
OTA_PORT = 3232
WATCHED = {
    "Lift Zustand",
    "Lift Referenz",
    "TV Lift Position Pulses",
    "TV Lift Position Percent",
    "Lift Geschwindigkeit",
    "Lift Sollgeschwindigkeit",
    "Lift Duty",
    "Lift Regelzyklen",
    "Lift Taktausreisser",
    "Loop Time",
    "Uptime",
}
LOG_TERMS = (
    "lift", "motor", "nvs", "persist", "safe_mode", "watchdog",
    "fault", "panic", "error",
)


class Recorder:
    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self.file = path.open("w", encoding="utf-8", buffering=1)

    def write(self, category: str, message: str) -> None:
        timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
        line = f"{timestamp} | {category:<6} | {message.strip()}"
        print(line, flush=True)
        self.file.write(line + "\n")

    def close(self) -> None:
        self.file.close()


async def port_open(port: int, timeout: float = 1.0) -> bool:
    try:
        _, writer = await asyncio.wait_for(
            asyncio.open_connection(HOST, port), timeout=timeout
        )
        writer.close()
        await writer.wait_closed()
        return True
    except (OSError, asyncio.TimeoutError):
        return False


async def disconnect_quietly(client: APIClient | None) -> None:
    if client is None:
        return
    try:
        await client.disconnect()
    except Exception:
        pass


async def run(minutes: int, output: Path) -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8-sig"))
    recorder = Recorder(output)
    deadline = asyncio.get_running_loop().time() + minutes * 60
    client: APIClient | None = None
    previous_ports: tuple[bool, bool] | None = None

    recorder.write("START", f"Mitschnitt für {minutes} min; Gerät {HOST}")
    recorder.write("FILE", str(output.resolve()))

    try:
        while asyncio.get_running_loop().time() < deadline:
            api_open, ota_open = await asyncio.gather(
                port_open(API_PORT), port_open(OTA_PORT)
            )
            ports = (api_open, ota_open)
            if ports != previous_ports:
                recorder.write(
                    "PORT",
                    f"API {API_PORT}={'offen' if api_open else 'zu'}, "
                    f"OTA {OTA_PORT}={'offen' if ota_open else 'zu'}",
                )
                previous_ports = ports

            if not api_open and client is not None:
                recorder.write("API", "Verbindung verloren")
                await disconnect_quietly(client)
                client = None

            if api_open and client is None:
                candidate = APIClient(
                    HOST, API_PORT, None,
                    client_info="Kiro Lift Failure Capture",
                    noise_psk=secrets["wohnzimmer_api_key"],
                )
                try:
                    await candidate.connect(login=True)
                    entities, _ = await candidate.list_entities_services()
                    names = {entity.key: entity.name for entity in entities}

                    def on_log(entry) -> None:
                        message = (
                            entry.message.decode("utf-8", "replace")
                            if isinstance(entry.message, bytes)
                            else str(entry.message)
                        )
                        if any(term in message.lower() for term in LOG_TERMS):
                            recorder.write("LOG", message)

                    def on_state(state) -> None:
                        name = names.get(state.key, "")
                        if name not in WATCHED:
                            return
                        if hasattr(state, "state"):
                            value = state.state
                        elif hasattr(state, "position"):
                            value = f"position={state.position}"
                        else:
                            value = repr(state)
                        recorder.write("STATE", f"{name}={value}")

                    candidate.subscribe_logs(on_log)
                    candidate.subscribe_states(on_state)
                    client = candidate
                    recorder.write("API", "verbunden; Logs und Zustände abonniert")
                except Exception as error:
                    recorder.write("API", f"Verbindungsversuch fehlgeschlagen: {error}")
                    await disconnect_quietly(candidate)

            await asyncio.sleep(1)
    finally:
        await disconnect_quietly(client)
        recorder.write("ENDE", "Mitschnitt beendet")
        recorder.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minutes", type=int, default=15)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.minutes <= 0:
        parser.error("--minutes muss größer als 0 sein")

    output = args.output or Path("logs") / (
        "lift-failure-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"
    )
    try:
        asyncio.run(run(args.minutes, output))
    except KeyboardInterrupt:
        print("Mitschnitt durch Benutzer beendet.")


if __name__ == "__main__":
    main()
