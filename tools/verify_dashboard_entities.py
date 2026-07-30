"""Prueft, ob alle im Dashboard referenzierten Entitaeten am Geraet existieren.

Liest die Dashboard-YAMLs, sammelt jede entity-Referenz und vergleicht sie mit
den Entitaeten, die der Controller ueber die ESPHome-API meldet. So fallen
Tippfehler und veraltete IDs auf, bevor Home Assistant "nicht verfuegbar"
anzeigt.

Aufruf aus dem Projektwurzelverzeichnis:
    python tools/verify_dashboard_entities.py
"""

import asyncio
import re
from pathlib import Path

import yaml
from aioesphomeapi import APIClient

DEVICE_HOST = "192.168.1.12"
DEVICE_PREFIX = "wohnzimmer_controller_"

DASHBOARDS = [
    Path("home-assistant/config/dashboards/livingroom.yaml"),
]

# ESPHome-Entitaetstyp -> Home-Assistant-Domain. Textsensoren landen in HA
# ebenfalls in der sensor-Domain.
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

ENTITY_PATTERN = re.compile(r"^[a-z_]+\.[a-z0-9_]+$")


def collect_referenced(path: Path) -> set[str]:
    """Sammelt rekursiv alle entity-Werte aus einer Dashboard-Datei."""
    found: set[str] = set()

    def walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                if key in ("entity", "entity_id") and isinstance(value, str):
                    found.add(value)
                elif key == "entities" and isinstance(value, list):
                    for item in value:
                        if isinstance(item, str):
                            found.add(item)
                        else:
                            walk(item)
                else:
                    walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(yaml.safe_load(path.read_text(encoding="utf-8")))
    return {entity for entity in found if ENTITY_PATTERN.match(entity)}


async def main() -> None:
    secrets = yaml.safe_load(Path("secrets.yaml").read_text(encoding="utf-8"))
    client = APIClient(DEVICE_HOST, 6053, None,
                       client_info="Kiro Dashboard Check",
                       noise_psk=secrets["wohnzimmer_api_key"])
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        available: set[str] = set()
        for entity in entities:
            domain = DOMAIN_BY_INFO.get(type(entity).__name__)
            if domain is not None:
                available.add(f"{domain}.{DEVICE_PREFIX}{entity.object_id}")
    finally:
        await client.disconnect()

    print(f"Geraet meldet {len(available)} Entitaeten.\n")
    problems = 0
    for path in DASHBOARDS:
        if not path.exists():
            print(f"{path}: nicht vorhanden, uebersprungen")
            continue
        referenced = collect_referenced(path)
        # Nur Controller-Entitaeten pruefen; fremde Entitaeten kennt das
        # Geraet naturgemaess nicht.
        own = {e for e in referenced if DEVICE_PREFIX in e}
        foreign = referenced - own
        missing = sorted(own - available)
        print(f"{path}")
        print(f"  referenziert: {len(referenced)} (davon Controller: {len(own)})")
        if foreign:
            print(f"  nicht vom Controller (ungeprueft): {sorted(foreign)}")
        if missing:
            problems += len(missing)
            print("  FEHLEND:")
            for entity in missing:
                print(f"    - {entity}")
        else:
            print("  alle Controller-Entitaeten vorhanden")
        print()

    print("ERGEBNIS: " + ("alles konsistent" if problems == 0
                          else f"{problems} fehlende Entitaet(en)"))


asyncio.run(main())
