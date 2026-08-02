"""Reboot-Test: 3x Neustart, NVS-Restore pruefen."""
import urllib.request, json, time, sys

BASE = "http://192.168.1.8:8123"
TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI3ZTU0Yzg3YjljNzI0ZWE0OWRhMjBmZWNmYTA5MzY5NiIsImlhdCI6MTc4NTQwMzE4MSwiZXhwIjoyMTAwNzYzMTgxfQ.mocNfp3ti4bETh30yMxmswMu1EBtbzQgYNEGGEimpdE"


def api_call(path, payload=None):
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
    api_call(f"services/{domain}/{service}", data)


def wait_for_device():
    for attempt in range(30):
        try:
            s = get_state("select.wohnzimmer_controller_livingroom_light_scene")
            if s["state"] not in ("unavailable", "unknown"):
                return True
        except Exception:
            pass
        time.sleep(3)
        sys.stdout.write(".")
        sys.stdout.flush()
    print()
    return False


def reboot_and_check(num):
    print(f"\n=== REBOOT {num}/3 ===")
    # Bekannten Zustand setzen
    call_service("select", "select_option", {
        "entity_id": "select.wohnzimmer_controller_livingroom_light_scene",
        "option": "Vitrine",
    })
    call_service("number", "set_value", {
        "entity_id": "number.wohnzimmer_controller_livingroom_effect_intensity",
        "value": 45,
    })
    # Auf NVS-Sync warten (5s Intervall im ESP)
    print("  Warte 8s auf NVS-Sync...")
    time.sleep(8)

    # Reboot
    print("  Reboot...")
    call_service("button", "press", {
        "entity_id": "button.wohnzimmer_controller_controller_restart",
    })
    time.sleep(8)

    # Warte auf Rueckkehr
    sys.stdout.write("  Warte auf Device")
    if not wait_for_device():
        print("\n  FEHLER: Device kam nicht zurueck!")
        return False
    print(" online!")

    # Warte auf apply_light_scene (on_boot delay 2s + Szenenanwendung)
    time.sleep(4)

    # Pruefe
    scene = get_state("select.wohnzimmer_controller_livingroom_light_scene")["state"]
    intensity = get_state("number.wohnzimmer_controller_livingroom_effect_intensity")["state"]
    glass = get_state("light.wohnzimmer_controller_vitrine_glass")
    spots = get_state("light.wohnzimmer_controller_vitrine_spots")
    sideboard = get_state("light.wohnzimmer_controller_sideboard")

    print(f"  Scene: {scene}")
    print(f"  Intensity: {intensity}")
    print(f"  Glass: state={glass['state']}, effect={glass['attributes'].get('effect')}")
    print(f"  Spots: state={spots['state']}, effect={spots['attributes'].get('effect')}")
    print(f"  Sideboard: state={sideboard['state']}")

    ok = True
    if scene != "Vitrine":
        print(f"  FAIL: Scene={scene} (erwartet Vitrine)")
        ok = False
    if abs(float(intensity) - 45.0) > 1:
        print(f"  FAIL: Intensity={intensity} (erwartet 45)")
        ok = False
    if glass["state"] != "on":
        print(f"  FAIL: Glass ist off")
        ok = False
    if glass["attributes"].get("effect") != "Museum":
        print(f"  FAIL: Glass effect={glass['attributes'].get('effect')} (erwartet Museum)")
        ok = False
    if spots["state"] != "on":
        print(f"  FAIL: Spots ist off")
        ok = False
    if spots["attributes"].get("effect") != "Museum":
        print(f"  FAIL: Spots effect={spots['attributes'].get('effect')} (erwartet Museum)")
        ok = False
    if sideboard["state"] != "off":
        print(f"  FAIL: Sideboard ist on (erwartet off bei Vitrine)")
        ok = False

    if ok:
        print(f"  PASS Reboot {num}/3")
    return ok


# ---- Main ----
print("Reboot-Test: 3x Neustart mit Vitrine @ 45%")
print("Prueft NVS-Restore von Szene, Intensitaet und Licht-Zustaenden")

for i in range(1, 4):
    if not reboot_and_check(i):
        sys.exit(1)

# Aufraemen
call_service("select", "select_option", {
    "entity_id": "select.wohnzimmer_controller_livingroom_light_scene",
    "option": "Manual",
})
print("\n=== ALLE 3 REBOOT-TESTS BESTANDEN ===")
