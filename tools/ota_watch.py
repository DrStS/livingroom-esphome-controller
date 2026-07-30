"""Wartet auf ein OTA-Fenster und spielt die Firmware dann sofort auf.

Zweck: Steckt der Controller im Safe Mode, ist er nur zeitweise erreichbar --
er laeuft einige Minuten, startet neu, scheitert und kommt wieder. Der
OTA-Port ist dabei nur in Fenstern offen. Von Hand ist das kaum zu treffen,
deshalb pollt dieses Skript im Sekundentakt und startet den Upload in der
Sekunde, in der der Port aufgeht.

Wichtig: Ein Fehlschlag ist harmlos. Der Safe Mode bleibt danach erreichbar,
ein erneuter Versuch ist jederzeit moeglich. Das Geraet muss dafuer nicht
ausgebaut werden.

Aufruf aus dem Projektwurzelverzeichnis:
    python tools/ota_watch.py
    python tools/ota_watch.py --minutes 30
"""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
from datetime import datetime

HOST = "192.168.1.12"
OTA_PORT = 3232
API_PORT = 6053


def port_open(host: str, port: int, timeout: float = 2.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def stamp() -> str:
    return datetime.now().strftime("%H:%M:%S")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minutes", type=int, default=20,
                        help="maximale Wartezeit (Vorgabe 20)")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    deadline = time.time() + args.minutes * 60
    print(f"Warte auf ein OTA-Fenster an {args.host}:{OTA_PORT} "
          f"(bis zu {args.minutes} min).")
    print("Der Controller muss dafuer eingeschaltet und am Netzwerk sein.\n")

    attempts = 0
    seen_offline = False
    while time.time() < deadline:
        if port_open(args.host, OTA_PORT):
            attempts += 1
            print(f"\n{stamp()}  OTA-Port offen -> Upload Versuch {attempts}")
            result = subprocess.run(
                ["esphome", "upload", "livingroom.yaml", "--device", args.host],
                capture_output=True, text=True,
            )
            output = result.stdout + result.stderr
            if "OTA successful" in output:
                print(f"{stamp()}  Upload erfolgreich.")
                break
            print(f"{stamp()}  Upload fehlgeschlagen, versuche es weiter.")
            for line in output.splitlines():
                if "rror" in line or "ailed" in line:
                    print(f"          {line.strip()}")
            time.sleep(5)
        else:
            if not seen_offline:
                print(f"{stamp()}  kein OTA-Port -- warte auf das naechste Fenster")
                seen_offline = True
            time.sleep(2)
    else:
        print(f"\nInnerhalb von {args.minutes} min kein OTA-Fenster erwischt.")
        print("Haengt der Controller am Strom und am Netzwerk?")
        sys.exit(1)

    # Nach dem Upload zeigt der API-Port, ob die Firmware diesmal durchstartet.
    print("\nWarte auf die API (das entscheidet, ob der Start geklappt hat) ...")
    for i in range(24):
        time.sleep(5)
        if port_open(args.host, API_PORT):
            print(f"{stamp()}  API-Port offen nach etwa {(i + 1) * 5} s -- "
                  f"die Firmware laeuft wieder.")
            return
        print(f"          warte ... {(i + 1) * 5} s")
    print("\nAPI kam nicht hoch: der Start scheitert weiterhin.")
    print("Der Safe Mode bleibt erreichbar, ein weiterer Versuch ist moeglich.")
    sys.exit(2)


main()
