"""Home-Assistant-Pipeline: Zugang pruefen, Konfiguration ausrollen, verifizieren.

Ersetzt das haendische Kopieren von YAML-Dateien in die Oberflaeche. Alles, was
Home Assistant braucht, liegt versioniert unter home-assistant/config/ und wird
von hier aus per SSH auf den Host gebracht.

Zwei Kanaele, absichtlich getrennt:
  SSH/SCP  -> Dateien nach /config schreiben (kann die REST-API nicht)
  REST-API -> pruefen, neu laden, neu starten, Entitaeten abfragen

Zugangsdaten stehen in secrets.yaml (nicht im Git). Fehlende Werte melde ich mit
einem Hinweis, wie sie zu beschaffen sind, statt mit einem Stacktrace.

Aufruf immer aus dem Projektwurzelverzeichnis:

    python tools/ha.py check                 Zugang pruefen (erster Schritt)
    python tools/ha.py keygen                SSH-Schluessel fuer den Zugang anlegen
    python tools/ha.py deploy                Dashboards ausrollen
    python tools/ha.py deploy --core         zusaetzlich configuration.yaml
    python tools/ha.py deploy --esphome      ESPHome-Konfiguration mitkopieren
    python tools/ha.py validate              Konfiguration von HA pruefen lassen
    python tools/ha.py reload                Automatisierungen/Szenen neu laden
    python tools/ha.py reload --integration  ESPHome-Integration neu laden
    python tools/ha.py restart               Home Assistant neu starten
    python tools/ha.py entities              alle Controller-Entitaeten mit Zustand
    python tools/ha.py verify                Dashboard gegen die HA-Registry pruefen
    python tools/ha.py logs                  Fehlerprotokoll auszugsweise
    python tools/ha.py setup                 Erstinstallation komplett ausrollen

Nichts davon bewegt Hardware.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parent.parent
SECRETS = ROOT / "secrets.yaml"
MANIFEST = ROOT / "home-assistant" / "deploy-manifest.yaml"

# Praefix, das Home Assistant den ESPHome-Entitaeten voranstellt (Geraetename).
DEVICE_PREFIX = "wohnzimmer_controller_"

OK = "  OK   "
FAIL = " FEHLER"
WARN = " HINWEIS"


# =============================================================================
# Konfiguration
# =============================================================================
class Settings:
    """Verbindungsdaten aus secrets.yaml, mit brauchbaren Vorgaben."""

    def __init__(self, data: dict[str, Any]) -> None:
        self.host: str = str(data.get("ha_host") or "homeassistant.local")
        self.port: int = int(data.get("ha_port") or 8123)
        self.scheme: str = str(data.get("ha_scheme") or "http")
        self.token: str | None = data.get("ha_token") or None
        self.ssh_user: str = str(data.get("ha_ssh_user") or "root")
        self.ssh_port: int = int(data.get("ha_ssh_port") or 22)
        self.ssh_key: str | None = data.get("ha_ssh_key") or None
        self.config_dir: str = str(data.get("ha_config_dir") or "/config")

    @property
    def base_url(self) -> str:
        return f"{self.scheme}://{self.host}:{self.port}"

    @property
    def ssh_target(self) -> str:
        return f"{self.ssh_user}@{self.host}"


def load_settings() -> Settings:
    if not SECRETS.exists():
        die(f"{SECRETS.name} fehlt. Aus secrets.example.yaml kopieren und ausfuellen.")
    # utf-8-sig: unter Windows setzen manche Editoren und PowerShell ein BOM,
    # an dem der YAML-Parser sonst haengt.
    data = yaml.safe_load(SECRETS.read_text(encoding="utf-8-sig")) or {}
    return Settings(data)


def load_manifest() -> dict[str, Any]:
    if not MANIFEST.exists():
        die(f"{MANIFEST} fehlt.")
    return yaml.safe_load(MANIFEST.read_text(encoding="utf-8")) or {}


def die(message: str, hint: str | None = None) -> None:
    print(f"\n{FAIL} {message}")
    if hint:
        print(f"\n{hint}")
    sys.exit(1)


# =============================================================================
# SSH-Kanal
# =============================================================================
def ssh_base(cfg: Settings) -> list[str]:
    """Gemeinsame Optionen. BatchMode verhindert, dass ein Passwortprompt die
    Pipeline blockiert -- ohne gueltigen Schluessel soll sie sofort scheitern."""
    args = [
        "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=accept-new",
        "-o", "ConnectTimeout=10",
    ]
    if cfg.ssh_key:
        args += ["-i", str(Path(cfg.ssh_key).expanduser())]
    return args


def ssh_run(cfg: Settings, command: str, check: bool = True) -> subprocess.CompletedProcess:
    args = ["ssh", *ssh_base(cfg), "-p", str(cfg.ssh_port), cfg.ssh_target, command]
    result = subprocess.run(args, capture_output=True, text=True)
    if check and result.returncode != 0:
        die(
            f"SSH-Befehl fehlgeschlagen (Code {result.returncode}): {command}\n"
            f"{result.stderr.strip()}",
            ssh_hint(cfg),
        )
    return result


def scp_put(cfg: Settings, local: Path, remote: str) -> None:
    args = [
        "scp", *ssh_base(cfg), "-P", str(cfg.ssh_port),
        str(local), f"{cfg.ssh_target}:{remote}",
    ]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        die(f"Kopieren von {local.name} fehlgeschlagen:\n{result.stderr.strip()}", ssh_hint(cfg))


def ssh_hint(cfg: Settings) -> str:
    return (
        "So bekommt die Pipeline SSH-Zugang:\n"
        "  1. In Home Assistant das Add-on 'Advanced SSH & Web Terminal' installieren\n"
        "     (Einstellungen -> Add-ons -> Add-on Store).\n"
        "  2. python tools/ha.py keygen ausfuehren und den angezeigten Public Key\n"
        "     in der Add-on-Konfiguration unter authorized_keys eintragen.\n"
        "  3. Im Add-on 'Protection mode' ausschalten und den Port auf\n"
        f"     {cfg.ssh_port} setzen, dann starten.\n"
        f"  4. In secrets.yaml ha_host, ha_ssh_user ({cfg.ssh_user}) und ha_ssh_key pflegen.\n"
        "Details: docs/home-assistant-pipeline.md"
    )


# =============================================================================
# REST-API-Kanal
# =============================================================================
def api(cfg: Settings, path: str, payload: dict | None = None,
        timeout: int = 30) -> Any:
    """Ruft die Home-Assistant-REST-API auf. payload gesetzt -> POST."""
    if not cfg.token:
        die(
            "Kein ha_token in secrets.yaml.",
            "So wird das Token erzeugt:\n"
            "  1. In Home Assistant mit dem Automatisierungs-Benutzer anmelden.\n"
            "  2. Unten links auf den Benutzernamen -> Sicherheit ->\n"
            "     'Langlebige Zugriffs-Tokens' -> Token erstellen.\n"
            "  3. Den Wert in secrets.yaml als ha_token eintragen.\n"
            "Das Token gilt unbegrenzt und kann an derselben Stelle widerrufen werden.",
        )
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(
        f"{cfg.base_url}/api/{path.lstrip('/')}",
        data=data,
        method="POST" if data is not None else "GET",
        headers={
            "Authorization": f"Bearer {cfg.token}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        if error.code == 401:
            die("Home Assistant lehnt das Token ab (401).",
                "Token in secrets.yaml pruefen oder ein neues erzeugen. "
                "Ein widerrufenes Token faellt sonst erst hier auf.")
        die(f"API-Fehler {error.code} bei {path}: {error.reason}")
    except urllib.error.URLError as error:
        die(f"Home Assistant unter {cfg.base_url} nicht erreichbar: {error.reason}",
            "Laeuft Home Assistant? Stimmt ha_host in secrets.yaml?")
    if not body:
        return None
    try:
        return json.loads(body)
    except json.JSONDecodeError:
        return body


def api_soft(cfg: Settings, path: str, timeout: int = 8) -> Any | None:
    """Wie api(), gibt aber None zurueck statt abzubrechen. Fuer Zustaende, in
    denen ein Fehlschlag erwartbar ist -- etwa waehrend eines Neustarts."""
    if not cfg.token:
        return None
    request = urllib.request.Request(
        f"{cfg.base_url}/api/{path.lstrip('/')}",
        headers={"Authorization": f"Bearer {cfg.token}"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except Exception:
        return None


def call_service(cfg: Settings, domain: str, service: str,
                 data: dict | None = None) -> Any:
    return api(cfg, f"services/{domain}/{service}", data or {})


def get_states(cfg: Settings) -> dict[str, dict]:
    states = api(cfg, "states")
    return {item["entity_id"]: item for item in states}


def tcp_open(host: str, port: int, timeout: float = 5.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def resolve_ipv4(host: str) -> str | None:
    """Loest den Namen bevorzugt auf IPv4 auf. Bei mDNS-Namen liefert Windows
    sonst gern eine link-local IPv6-Adresse, mit der urllib nichts anfangen
    kann."""
    try:
        infos = socket.getaddrinfo(host, None, socket.AF_INET)
    except socket.gaierror:
        return None
    return infos[0][4][0] if infos else None


# =============================================================================
# Kommando: check
# =============================================================================
def cmd_check(cfg: Settings, args: argparse.Namespace) -> None:
    print(f"Home Assistant: {cfg.base_url}")
    print(f"SSH:            {cfg.ssh_target}:{cfg.ssh_port}\n")

    address = resolve_ipv4(cfg.host)
    if address:
        print(f"{OK} Name aufgeloest: {cfg.host} -> {address}")
    else:
        print(f"{FAIL} {cfg.host} laesst sich nicht auf eine IPv4-Adresse aufloesen")
        print("        Feste IP-Adresse des Hosts als ha_host in secrets.yaml eintragen.")

    problems = 0

    if tcp_open(cfg.host, cfg.port):
        print(f"{OK} Oberflaeche erreichbar (Port {cfg.port})")
    else:
        print(f"{FAIL} Port {cfg.port} geschlossen -- Home Assistant laeuft nicht oder falscher Host")
        problems += 1

    if tcp_open(cfg.host, cfg.ssh_port):
        print(f"{OK} SSH-Port {cfg.ssh_port} offen")
        result = ssh_run(cfg, "echo bereit", check=False)
        if result.returncode == 0 and "bereit" in result.stdout:
            print(f"{OK} SSH-Anmeldung mit Schluessel erfolgreich")
            version = ssh_run(cfg, "cat /config/.HA_VERSION 2>/dev/null || true", check=False)
            if version.stdout.strip():
                print(f"{OK} Konfigurationsverzeichnis gefunden, HA-Version {version.stdout.strip()}")
            else:
                print(f"{WARN} /config nicht sichtbar -- im SSH-Add-on 'Protection mode' ausschalten")
                problems += 1
        else:
            print(f"{FAIL} SSH-Anmeldung abgelehnt")
            print(f"        {result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ''}")
            problems += 1
    else:
        print(f"{FAIL} SSH-Port {cfg.ssh_port} geschlossen -- Add-on nicht installiert oder gestoppt")
        problems += 1

    # Bewusst tolerant: check soll ein vollstaendiges Bild liefern und nicht
    # beim ersten Problem abbrechen.
    if not cfg.token or cfg.token.startswith("REPLACE_WITH"):
        print(f"{FAIL} ha_token in secrets.yaml noch nicht gesetzt -- API-Kanal ungenutzt")
        print("        Token erzeugen: als Automatisierungs-Benutzer anmelden, unten links")
        print("        auf den Namen -> Sicherheit -> Langlebige Zugriffs-Tokens.")
        problems += 1
    else:
        config = api_soft(cfg, "config")
        if config is None:
            print(f"{FAIL} API-Token wird abgelehnt oder die Oberflaeche antwortet nicht")
            problems += 1
        else:
            print(f"{OK} API-Token gueltig, HA {config.get('version')} "
                  f"({config.get('location_name')})")
            states = api_soft(cfg, "states", timeout=20) or []
            own = [item["entity_id"] for item in states if DEVICE_PREFIX in item["entity_id"]]
            print(f"{OK} {len(states)} Entitaeten insgesamt, davon {len(own)} vom Controller")
            if not own:
                print("        Der Controller ist in Home Assistant noch nicht eingebunden:")
                print("        Einstellungen -> Geraete & Dienste -> ESPHome, Host 192.168.1.12,")
                print("        Port 6053, Schluessel = wohnzimmer_api_key aus secrets.yaml.")

    print()
    if problems:
        print(f"{problems} offene Punkte. Anleitung: docs/home-assistant-pipeline.md")
        sys.exit(1)
    print("Zugang vollstaendig. Naechster Schritt: python tools/ha.py setup")


# =============================================================================
# Kommando: keygen
# =============================================================================
def cmd_keygen(cfg: Settings, args: argparse.Namespace) -> None:
    key_path = Path(cfg.ssh_key).expanduser() if cfg.ssh_key else Path.home() / ".ssh" / "ha_livingroom"
    public = key_path.with_suffix(key_path.suffix + ".pub")

    if key_path.exists() and not args.force:
        print(f"Schluessel existiert schon: {key_path}")
    else:
        key_path.parent.mkdir(parents=True, exist_ok=True)
        if key_path.exists():
            key_path.unlink()
            if public.exists():
                public.unlink()
        result = subprocess.run(
            ["ssh-keygen", "-t", "ed25519", "-N", "", "-C",
             "livingroom-pipeline", "-f", str(key_path)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            die(f"ssh-keygen fehlgeschlagen:\n{result.stderr.strip()}")
        print(f"Neuer Schluessel angelegt: {key_path}")

    print("\nDiesen Public Key im Add-on 'Advanced SSH & Web Terminal' unter")
    print("authorized_keys eintragen:\n")
    print(public.read_text(encoding="utf-8").strip())
    print(f"\nUnd in secrets.yaml hinterlegen:\n\n  ha_ssh_key: \"{key_path.as_posix()}\"\n")


# =============================================================================
# Kommando: deploy
# =============================================================================
def shell_quote(value: str) -> str:
    """Minimales Quoting fuer die Busybox-Shell auf dem Host."""
    return "'" + value.replace("'", "'\\''") + "'"


def remote_backup(cfg: Settings, targets: list[str], stamp: str) -> str:
    """Sichert die zu ueberschreibenden Dateien auf dem Host.

    Ein Deploy soll niemals der Punkt sein, an dem eine gewachsene
    Konfiguration verloren geht. Nicht vorhandene Dateien werden uebersprungen.
    """
    backup = f"{cfg.config_dir}/.deploy-backup/{stamp}"
    listing = " ".join(shell_quote(t) for t in targets)
    script = (
        f"set -e; B={shell_quote(backup)}; mkdir -p \"$B\"; "
        f"for f in {listing}; do "
        f"  if [ -e {shell_quote(cfg.config_dir)}/\"$f\" ]; then "
        f"    mkdir -p \"$B/$(dirname \"$f\")\"; "
        f"    cp -a {shell_quote(cfg.config_dir)}/\"$f\" \"$B/$f\"; "
        f"  fi; "
        f"done"
    )
    ssh_run(cfg, script)
    return backup


class HALoader(yaml.SafeLoader):
    """YAML-Loader, der die Home-Assistant-eigenen Tags kennt.

    Ohne das scheitert jede lokale Pruefung an "!include" und Verwandten. Die
    Tags werden bewusst nur als Platzhalter aufgeloest -- geprueft wird die
    Syntax, nicht der Inhalt der eingebundenen Dateien.
    """


def _ha_tag_placeholder(loader: yaml.Loader, node: yaml.Node) -> str:
    if isinstance(node, yaml.ScalarNode):
        return f"<{node.tag} {loader.construct_scalar(node)}>"
    return f"<{node.tag}>"


for _tag in ("!include", "!include_dir_list", "!include_dir_merge_list",
             "!include_dir_named", "!include_dir_merge_named",
             "!secret", "!env_var", "!input"):
    HALoader.add_constructor(_tag, _ha_tag_placeholder)


def check_yaml_syntax(paths: list[Path]) -> None:
    """Prueft die YAML-Syntax vor dem Kopieren.

    Ein Syntaxfehler in configuration.yaml faellt sonst erst beim Neustart auf --
    und dann startet Home Assistant nicht mehr. Diese Pruefung kostet nichts und
    verhindert genau diesen Zustand.
    """
    broken = 0
    for path in paths:
        if path.suffix not in (".yaml", ".yml"):
            continue
        try:
            yaml.load(path.read_text(encoding="utf-8"), Loader=HALoader)
        except yaml.YAMLError as error:
            broken += 1
            print(f"{FAIL} {path.relative_to(ROOT).as_posix()}\n{error}\n")
    if broken:
        die(f"{broken} Datei(en) mit YAML-Fehlern -- nichts kopiert.")


def prepare_source(source: Path, temp_dir: Path) -> Path:
    """Gibt die Datei zurueck, die tatsaechlich kopiert wird.

    Sonderfall secrets.yaml: die Datei enthaelt neben den ESPHome-Schluesseln
    auch die Zugangsdaten dieser Pipeline (ha_token, ha_ssh_key). Auf dem Host
    braucht ESPHome davon nichts, und ein API-Token gehoert nicht in ein
    Verzeichnis, das ueber Add-ons und Backups zugaenglich ist. Deshalb wird
    eine auf die ESPHome-Schluessel reduzierte Fassung uebertragen.
    """
    if source.name != "secrets.yaml":
        return source
    data = yaml.safe_load(source.read_text(encoding="utf-8")) or {}
    filtered = {k: v for k, v in data.items() if not k.startswith("ha_")}
    temp_dir.mkdir(parents=True, exist_ok=True)
    reduced = temp_dir / "secrets.yaml"
    header = ("# Von tools/ha.py erzeugt. Die Zugangsdaten der Pipeline (ha_*)\n"
              "# sind absichtlich nicht enthalten.\n")
    reduced.write_text(header + yaml.safe_dump(filtered, allow_unicode=True),
                       encoding="utf-8")
    return reduced


def collect_files(manifest: dict, include_core: bool) -> list[tuple[Path, str]]:
    pairs: list[tuple[Path, str]] = []
    for entry in manifest.get("files", []):
        if entry.get("core") and not include_core:
            continue
        source = ROOT / entry["source"]
        if not source.exists():
            die(f"Quelldatei aus dem Manifest fehlt: {entry['source']}")
        pairs.append((source, entry["target"]))
    return pairs


def cmd_deploy(cfg: Settings, args: argparse.Namespace) -> None:
    manifest = load_manifest()
    pairs = collect_files(manifest, args.core)

    if args.esphome:
        block = manifest.get("esphome", {})
        target_dir = block.get("target", "esphome")
        for name in block.get("files", []):
            source = ROOT / name
            if source.exists():
                pairs.append((source, f"{target_dir}/{Path(name).name}"))
        for folder in block.get("directories", []):
            base = ROOT / folder
            for source in sorted(base.rglob("*")):
                if source.is_file() and "__pycache__" not in source.parts:
                    relative = source.relative_to(ROOT).as_posix()
                    pairs.append((source, f"{target_dir}/{relative}"))

    print(f"Ziel: {cfg.ssh_target}:{cfg.config_dir}")
    print(f"{len(pairs)} Datei(en){' inklusive Kernkonfiguration' if args.core else ''}"
          f"{' inklusive ESPHome' if args.esphome else ''}\n")
    for source, target in pairs:
        print(f"  {source.relative_to(ROOT).as_posix()}  ->  {cfg.config_dir}/{target}")

    print("\nPruefe YAML-Syntax ...")
    check_yaml_syntax([source for source, _ in pairs])
    print(f"{OK} alle YAML-Dateien syntaktisch in Ordnung")

    if args.dry_run:
        print("\nProbelauf: nichts kopiert.")
        return

    # Zielverzeichnisse anlegen: aus dem Manifest plus alles, was die Zielpfade
    # implizit brauchen.
    folders = set(manifest.get("directories", []))
    folders.update(str(Path(t).parent.as_posix()) for _, t in pairs
                   if Path(t).parent.as_posix() != ".")
    if folders:
        listing = " ".join(shell_quote(f"{cfg.config_dir}/{f}") for f in sorted(folders))
        ssh_run(cfg, f"mkdir -p {listing}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = remote_backup(cfg, [t for _, t in pairs], stamp)
    print(f"\nSicherung der bisherigen Dateien: {backup}")

    print("\nKopiere ...")
    temp_dir = ROOT / ".ha-deploy-tmp"
    try:
        for source, target in pairs:
            scp_put(cfg, prepare_source(source, temp_dir), f"{cfg.config_dir}/{target}")
            note = "  (ohne die ha_*-Zugangsdaten)" if source.name == "secrets.yaml" else ""
            print(f"{OK} {target}{note}")
    finally:
        for leftover in temp_dir.glob("*"):
            leftover.unlink()
        if temp_dir.exists():
            temp_dir.rmdir()

    removed = []
    for stale in manifest.get("obsolete", []):
        path = f"{cfg.config_dir}/{stale}"
        exists = ssh_run(cfg, f"test -e {shell_quote(path)} && echo ja || echo nein", check=False)
        if exists.stdout.strip() == "ja":
            ssh_run(cfg, f"cp -a {shell_quote(path)} {shell_quote(backup)}/ 2>/dev/null || true", check=False)
            ssh_run(cfg, f"rm -rf {shell_quote(path)}")
            removed.append(stale)
    if removed:
        print("\nAltlasten entfernt (vorher gesichert):")
        for name in removed:
            print(f"{OK} {name}")

    print("\nFertig. Weiter mit: python tools/ha.py validate")


# =============================================================================
# Kommando: validate / reload / restart / logs
# =============================================================================
def cmd_validate(cfg: Settings, args: argparse.Namespace) -> None:
    print("Home Assistant prueft die Konfiguration ...")
    result = api(cfg, "config/core/check_config", {}, timeout=120)
    if isinstance(result, dict) and result.get("result") == "valid":
        print(f"{OK} Konfiguration gueltig")
        return
    errors = (result or {}).get("errors") if isinstance(result, dict) else result
    print(f"{FAIL} Konfiguration fehlerhaft:\n")
    print(errors)
    sys.exit(1)


def cmd_reload(cfg: Settings, args: argparse.Namespace) -> None:
    if args.integration:
        states = get_states(cfg)
        anchor = next((e for e in states if e.startswith(f"sensor.{DEVICE_PREFIX}")), None)
        if anchor is None:
            die("Keine Controller-Entitaet in Home Assistant gefunden.",
                "Ist das Geraet unter Einstellungen -> Geraete & Dienste -> ESPHome "
                "hinzugefuegt? Host = IP des Controllers, Port 6053.")
        call_service(cfg, "homeassistant", "reload_config_entry", {"entity_id": anchor})
        print(f"{OK} ESPHome-Integration neu geladen (ueber {anchor})")
        print("     Danach 'python tools/ha.py verify' zum Nachpruefen.")
        return

    for domain in ("automation", "script", "scene"):
        call_service(cfg, domain, "reload")
        print(f"{OK} {domain} neu geladen")
    call_service(cfg, "homeassistant", "reload_core_config")
    print(f"{OK} Kernkonfiguration neu geladen")
    print("\nYAML-Dashboards liest Home Assistant beim Neuladen der Seite im Browser\n"
          "selbst neu ein -- dafuer ist kein Dienstaufruf notwendig.")


def cmd_restart(cfg: Settings, args: argparse.Namespace) -> None:
    cmd_validate(cfg, args)
    print("\nStarte Home Assistant neu ...")
    # Der Dienstaufruf kappt die eigene Verbindung: Home Assistant faehrt
    # herunter, bevor die Antwort vollstaendig raus ist. Ein Abbruch hier ist
    # also der Normalfall und kein Fehler -- entschieden wird ueber das
    # anschliessende Warten auf die Rueckkehr.
    request = urllib.request.Request(
        f"{cfg.base_url}/api/services/homeassistant/restart",
        data=b"{}",
        method="POST",
        headers={"Authorization": f"Bearer {cfg.token}",
                 "Content-Type": "application/json"},
    )
    try:
        urllib.request.urlopen(request, timeout=15).read()
    except urllib.error.HTTPError as error:
        if error.code == 401:
            die("Home Assistant lehnt das Token ab (401).")
        print(f"     Antwort {error.code} -- Neustart laeuft vermutlich trotzdem")
    except (urllib.error.URLError, TimeoutError, socket.timeout):
        pass

    for attempt in range(60):
        time.sleep(5)
        config = api_soft(cfg, "config")
        if isinstance(config, dict) and config.get("state") == "RUNNING":
            print(f"{OK} wieder erreichbar nach etwa {(attempt + 1) * 5} s, "
                  f"HA {config.get('version')}")
            return
        print(f"     warte ... ({(attempt + 1) * 5} s)")
    die("Home Assistant ist nach 5 Minuten nicht wieder erreichbar.",
        "Protokoll ansehen: python tools/ha.py logs")


def cmd_logs(cfg: Settings, args: argparse.Namespace) -> None:
    text = api(cfg, "error_log")
    lines = str(text).splitlines()
    selected = lines[-args.lines:]
    if args.filter:
        pattern = re.compile(args.filter, re.IGNORECASE)
        selected = [line for line in lines if pattern.search(line)][-args.lines:]
    if not selected:
        print("Protokoll ist leer -- keine Warnungen oder Fehler.")
        return
    print("\n".join(selected))


# =============================================================================
# Kommando: entities / verify
# =============================================================================
def cmd_entities(cfg: Settings, args: argparse.Namespace) -> None:
    states = get_states(cfg)
    own = {k: v for k, v in states.items() if DEVICE_PREFIX in k}
    if not own:
        die("Home Assistant kennt keine Entitaet des Controllers.",
            "Geraet unter Einstellungen -> Geraete & Dienste -> ESPHome hinzufuegen.")

    broken = {k: v for k, v in own.items() if v["state"] in ("unavailable", "unknown")}
    # Home Assistant haengt bei einer Namenskollision ein _2 an. Das passiert,
    # wenn ein alter Registry-Eintrag denselben Namen belegt -- der haeufigste
    # Grund fuer "Entitaet nicht gefunden" im Dashboard.
    duplicates = sorted(k for k in own if re.search(r"_\d+$", k))

    print(f"{len(own)} Controller-Entitaeten in Home Assistant\n")
    for entity_id in sorted(own):
        state = own[entity_id]["state"]
        mark = "!" if state in ("unavailable", "unknown") else " "
        if args.all or mark == "!":
            print(f" {mark} {entity_id:60} {state}")

    if duplicates:
        print(f"\n{WARN} Nummerierte Entitaeten gefunden -- Hinweis auf alte Registry-Eintraege:")
        for entity_id in duplicates:
            print(f"     {entity_id}")
        print("     Bereinigen: Geraet in Home Assistant loeschen und neu hinzufuegen")
        print("     (Einstellungen -> Geraete & Dienste -> ESPHome -> Geraet -> Loeschen).")

    if broken:
        print(f"\n{WARN} {len(broken)} Entitaeten ohne Wert (unavailable/unknown).")
        print("     Ist der Controller online? python tools/check_entity_states.py")
    elif not args.all:
        print(f"\n{OK} alle Entitaeten liefern Werte (--all zeigt die vollstaendige Liste)")


def collect_dashboard_entities(path: Path) -> set[str]:
    """Sammelt rekursiv jede entity-Referenz aus einer Dashboard-Datei."""
    pattern = re.compile(r"^[a-z_]+\.[a-z0-9_]+$")
    found: set[str] = set()

    def walk(node: Any) -> None:
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
    return {entity for entity in found if pattern.match(entity)}


def cmd_verify(cfg: Settings, args: argparse.Namespace) -> None:
    """Prueft die Dashboards gegen die tatsaechliche Registry von Home Assistant.

    Der Unterschied zu tools/verify_dashboard_entities.py ist entscheidend:
    dort wird gegen das GERAET geprueft, hier gegen HOME ASSISTANT. Genau dieser
    Vergleich trennt "Firmware liefert die Entitaet nicht" von "Home Assistant
    hat sie nicht registriert".
    """
    manifest = load_manifest()
    dashboards = [ROOT / e["source"] for e in manifest.get("files", [])
                  if e["target"].startswith("dashboards/")]
    states = get_states(cfg)

    problems = 0
    for path in dashboards:
        referenced = sorted(collect_dashboard_entities(path))
        missing = [e for e in referenced if e not in states]
        empty = [e for e in referenced
                 if e in states and states[e]["state"] in ("unavailable", "unknown")]
        print(f"{path.relative_to(ROOT).as_posix()}")
        print(f"  {len(referenced)} Referenzen")
        if missing:
            problems += len(missing)
            print(f"  {FAIL} in Home Assistant nicht vorhanden:")
            for entity in missing:
                print(f"       {entity}")
        if empty:
            print(f"  {WARN} vorhanden, aber ohne Wert:")
            for entity in empty:
                print(f"       {entity}")
        if not missing and not empty:
            print(f"  {OK} alle Referenzen vorhanden und mit Wert")
        print()

    if problems:
        print("Fehlende Entitaeten haben genau zwei Ursachen:\n"
              "  1. Das Geraet liefert sie nicht -> pruefen mit\n"
              "     python tools/verify_dashboard_entities.py\n"
              "  2. Das Geraet liefert sie, Home Assistant hat sie nicht registriert ->\n"
              "     python tools/ha.py reload --integration\n"
              "     reicht das nicht, das Geraet in Home Assistant loeschen und neu\n"
              "     hinzufuegen. Dabei verschwinden auch alte Registry-Eintraege.")
        sys.exit(1)
    print("Dashboards und Home Assistant sind deckungsgleich.")


# =============================================================================
# Kommando: setup -- Erstinstallation in einem Zug
# =============================================================================
def cmd_setup(cfg: Settings, args: argparse.Namespace) -> None:
    steps = [
        ("Zugang pruefen", lambda: cmd_check(cfg, args)),
        ("Konfiguration ausrollen", lambda: cmd_deploy(cfg, argparse.Namespace(
            core=True, esphome=args.esphome, dry_run=False))),
        ("Konfiguration pruefen", lambda: cmd_validate(cfg, args)),
        ("Home Assistant neu starten", lambda: cmd_restart(cfg, args)),
        ("Dashboards verifizieren", lambda: cmd_verify(cfg, args)),
    ]
    for index, (title, action) in enumerate(steps, start=1):
        print(f"\n{'=' * 70}\nSchritt {index}/{len(steps)}: {title}\n{'=' * 70}")
        action()
    print("\nSetup abgeschlossen.")


# =============================================================================
# CLI
# =============================================================================
def main() -> None:
    parser = argparse.ArgumentParser(
        prog="python tools/ha.py",
        description="Home-Assistant-Setup steuern: ausrollen, pruefen, neu laden.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("check", help="Erreichbarkeit, SSH und API-Token pruefen")

    keygen = sub.add_parser("keygen", help="SSH-Schluessel fuer den Zugang anlegen")
    keygen.add_argument("--force", action="store_true", help="bestehenden Schluessel ersetzen")

    deploy = sub.add_parser("deploy", help="Dateien nach /config kopieren")
    deploy.add_argument("--core", action="store_true",
                        help="configuration.yaml, automations, scripts und scenes mitnehmen")
    deploy.add_argument("--esphome", action="store_true",
                        help="ESPHome-Konfiguration nach /config/esphome kopieren")
    deploy.add_argument("--dry-run", action="store_true", help="nur anzeigen, nichts kopieren")

    sub.add_parser("validate", help="Konfiguration von Home Assistant pruefen lassen")

    reload_cmd = sub.add_parser("reload", help="Automatisierungen, Szenen oder Integration neu laden")
    reload_cmd.add_argument("--integration", action="store_true",
                            help="ESPHome-Integration neu laden (bei fehlenden Entitaeten)")

    sub.add_parser("restart", help="Home Assistant neu starten und auf Rueckkehr warten")

    entities = sub.add_parser("entities", help="Controller-Entitaeten mit Zustand auflisten")
    entities.add_argument("--all", action="store_true", help="auch die unauffaelligen zeigen")

    sub.add_parser("verify", help="Dashboards gegen die Registry von Home Assistant pruefen")

    logs = sub.add_parser("logs", help="Fehlerprotokoll von Home Assistant")
    logs.add_argument("--lines", type=int, default=40, help="Anzahl Zeilen (Vorgabe 40)")
    logs.add_argument("--filter", help="nur Zeilen mit diesem Muster")

    setup = sub.add_parser("setup", help="Erstinstallation komplett ausrollen")
    setup.add_argument("--esphome", action="store_true",
                       help="ESPHome-Konfiguration mitkopieren")

    args = parser.parse_args()
    cfg = load_settings()

    handlers = {
        "check": cmd_check,
        "keygen": cmd_keygen,
        "deploy": cmd_deploy,
        "validate": cmd_validate,
        "reload": cmd_reload,
        "restart": cmd_restart,
        "entities": cmd_entities,
        "verify": cmd_verify,
        "logs": cmd_logs,
        "setup": cmd_setup,
    }
    handlers[args.command](cfg, args)


if __name__ == "__main__":
    main()
