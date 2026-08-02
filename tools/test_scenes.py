"""Szenen-Test + Reboot-Test fuer die neuen Vitrine-Partitionen."""
import urllib.request, json, time, sys

BASE = "http://192.168.1.8:8123"
TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI3ZTU0Yzg3YjljNzI0ZWE0OWRhMjBmZWNmYTA5MzY5NiIsImlhdCI6MTc4NTQwMzE4MSwiZXhwIjoyMTAwNzYzMTgxfQ.mocNfp3ti4bETh30yMxmswMu1EBtbzQgYNEGGEimpdE"

LIGHTS = [
    "light.wohnzimmer_controller_vitrine_glass",
    "light.wohnzimmer_controller_vitrine_spots",
    "light.wohnzimmer_controller_sideboard",
]


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload else None
    req = urllib.request.Request(
        f"{BASE}/api/{path}",
        data=data,
        method="POST" if data else "GET",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
    )
    resp = urllib.request.urlopen(req, timeout=10)
    return json.loads(resp.read())


def get_state(entity_id):
    req = urllib.request.Request(
        f"{BASE}/api/states/{entity_id}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    resp = urllib.request.urlopen(req, timeout=10)
    return json.loads(resp.read())


def call_service(domain, service, data):
    api(f"services/{domain}/{service}", data)


def print_lights():
    for eid in LIGHTS:
        s = get_state(eid)
        name = eid.split(".")[1].replace("wohnzimmer_controller_", "")
        effect = s["attributes"].get("effect", "N/A")
        bri = s["attributes"].get("brightness", "N/A")
        print(f"  {name}: state={s['state']}, brightness={bri}, effect={effect}")


def set_scene(scene):
    call_service("select", "select_option", {
        "entity_id": "select.wohnzimmer_controller_livingroom_light_scene",
        "option": scene,
    })


def wait_for_device(timeout_s=90):
    """Wartet bis der ESP wieder online ist."""
    for attempt in range(timeout_s // 3):
        try:
            s = get_state("select.wohnzimmer_controller_livingroom_light_scene")
            if s["state"] not in ("unavailable", "unknown"):
                return True
        except Exception:
            pass
        time.sleep(3)
        print(f"  Warte... ({(attempt + 1) * 3}s)")
    return False


# ---- Hauptteil ----
print("=== Checking new entities ===")
print_lights()
s = get_state("select.wohnzimmer_controller_livingroom_light_scene")
print(f"Scene: {s['state']}")
s = get_state("number.wohnzimmer_controller_livingroom_effect_intensity")
print(f"Intensity: {s['state']}")

scenes = ["Vitrine", "Kaminfeuer", "Cinema", "Kaminfeuer + Vitrine", "Feuerwerk", "Aus"]
for scene in scenes:
    print(f"\n=== Scene: {scene} ===")
    set_scene(scene)
    time.sleep(3)
    print_lights()

# ---- Reboot-Tests ----
for reboot_num in range(1, 4):
    print(f"\n{'='*60}")
    print(f"=== REBOOT TEST {reboot_num}/3 ===")
    print(f"{'='*60}")

    # Bekannten Zustand setzen
    set_scene("Vitrine")
    call_service("number", "set_value", {
        "entity_id": "number.wohnzimmer_controller_livingroom_effect_intensity",
        "value": 45,
    })
    time.sleep(5)  # Warten auf NVS-Sync (5s interval)

    print("  Zustand gesetzt: Vitrine @ 45%")
    print("  Reboot wird ausgeloest...")
    call_service("button", "press", {
        "entity_id": "button.wohnzimmer_controller_controller_restart",
    })
    time.sleep(10)

    if not wait_for_device():
        print("  FEHLER: Device kam nicht zurueck!")
        sys.exit(1)

    time.sleep(3)  # Warte auf apply_light_scene nach Boot
    print("  Device ist zurueck. Pruefe Zustand:")
    s = get_state("select.wohnzimmer_controller_livingroom_light_scene")
    print(f"  Scene: {s['state']}")
    s = get_state("number.wohnzimmer_controller_livingroom_effect_intensity")
    print(f"  Intensity: {s['state']}")
    print_lights()

    # Validierung
    scene_state = get_state("select.wohnzimmer_controller_livingroom_light_scene")
    intensity_state = get_state("number.wohnzimmer_controller_livingroom_effect_intensity")
    glass_state = get_state("light.wohnzimmer_controller_vitrine_glass")
    spots_state = get_state("light.wohnzimmer_controller_vitrine_spots")

    errors = []
    if scene_state["state"] != "Vitrine":
        errors.append(f"Scene={scene_state['state']} (erwartet: Vitrine)")
    if abs(float(intensity_state["state"]) - 45.0) > 1:
        errors.append(f"Intensity={intensity_state['state']} (erwartet: 45)")
    if glass_state["state"] != "on":
        errors.append(f"Vitrine Glass={glass_state['state']} (erwartet: on)")
    if spots_state["state"] != "on":
        errors.append(f"Vitrine Spots={spots_state['state']} (erwartet: on)")
    if glass_state["attributes"].get("effect") != "Museum":
        errors.append(f"Glass effect={glass_state['attributes'].get('effect')} (erwartet: Museum)")
    if spots_state["attributes"].get("effect") != "Museum":
        errors.append(f"Spots effect={spots_state['attributes'].get('effect')} (erwartet: Museum)")

    if errors:
        print(f"\n  FEHLER bei Reboot {reboot_num}:")
        for e in errors:
            print(f"    - {e}")
        sys.exit(1)
    else:
        print(f"  OK Reboot {reboot_num}/3 bestanden")

# Aufraemen: Manual
set_scene("Manual")
print("\n=== ALLE TESTS BESTANDEN ===")
