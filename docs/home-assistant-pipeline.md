# Home Assistant neu aufsetzen und per Pipeline steuern

Ziel: das gesamte Home-Assistant-Setup liegt versioniert in diesem Repository
und wird mit einem Befehl ausgerollt. Kein Kopieren von YAML-Schnipseln in die
Oberflaeche mehr, kein Rätselraten, welche Fassung auf dem Host liegt.

Zwei Kanaele, absichtlich getrennt:

| Kanal | Wofuer | Warum nicht der andere |
|---|---|---|
| SSH/SCP | Dateien nach `/config` schreiben | Die REST-API kann keine Dateien schreiben. |
| REST-API | pruefen, neu laden, neu starten, Entitaeten abfragen | Ueber SSH waere das umstaendlich und weniger aussagekraeftig. |

Alle Befehle laufen aus dem Projektwurzelverzeichnis.

---

## Teil 1 -- Home Assistant neu installieren

Diese Schritte gehen nur von Hand, weil dabei Hardware und Anmeldedaten im
Spiel sind.

1. **Sicherung des alten Systems ziehen**, falls noch erreichbar:
   Einstellungen → System → Sicherungen → Sicherung erstellen, herunterladen.
   Sie wird nicht wieder eingespielt (sonst kaeme der alte Ballast zurueck),
   ist aber ein Rueckweg.

2. **Home Assistant OS neu schreiben.** Raspberry Pi Imager → *Other
   specific-purpose OS* → *Home automation* → *Home Assistant* → Version fuer
   Raspberry Pi 4. Ziel ist eine SSD per USB, keine SD-Karte: der Recorder
   schreibt dauernd, und SD-Karten sterben daran.

3. **Feste Adresse vergeben.** Im Router eine statische DHCP-Lease auf die
   MAC-Adresse des Pi setzen. Diese Adresse kommt spaeter als `ha_host` in
   `secrets.yaml`. Der Name `homeassistant.local` funktioniert oft, loest unter
   Windows aber gern auf eine link-local IPv6-Adresse auf, mit der die Pipeline
   nichts anfangen kann.

4. **Auf den ersten Start warten:**

   ```
   python tools/ha.py wait
   ```

   Meldet, sobald die Oberflaeche antwortet, und nennt die Adresse. Weicht sie
   von `ha_host` in `secrets.yaml` ab, wird das ausdruecklich gesagt -- dann
   greift die DHCP-Lease nicht. Der erste Start nach dem Schreiben des Abbilds
   dauert einige Minuten, weil das Dateisystem erst angelegt wird.

   Dann **Onboarding** unter `http://<adresse>:8123`. Eigenen Administrator
   anlegen, Standort, Zeitzone und Einheiten setzen. Bei "Geraete gefunden"
   nichts uebernehmen, das kommt spaeter kontrolliert.

5. **Zweiten Administrator fuer die Automatisierung anlegen.**
   Einstellungen → Personen → Reiter *Benutzer* → *Benutzer hinzufuegen*:
   - Name: `Pipeline`
   - Passwort: frei waehlbar, wird nur fuer die Anmeldung in Schritt 6 gebraucht
   - Administrator: ja (das Ausrollen braucht Administratorrechte)
   - "Nur lokaler Zugriff": an, wenn von aussen nichts nötig ist

   Getrennter Benutzer, weil das dann sichtbar getrennt ist: im Protokoll ist zu
   erkennen, was von der Automatisierung kommt, und der Zugang laesst sich
   einzeln widerrufen, ohne den eigenen Login anzufassen.

6. **Token erzeugen.** Als `Pipeline` anmelden (am einfachsten in einem privaten
   Browserfenster, damit die eigene Sitzung bestehen bleibt). Dann unten links
   auf den Benutzernamen → Reiter *Sicherheit* → ganz unten *Langlebige
   Zugriffs-Tokens* → *Token erstellen* → Namen vergeben.

   **Das Token ist nicht das Passwort.** Es ist ein eigener, sehr langer String
   (JWT, rund 180 Zeichen, beginnt mit `eyJ`), der genau hier erzeugt wird. Nur
   dieser String kommt nach `secrets.yaml`; das Passwort des Benutzers wird
   danach nicht mehr gebraucht.

   Gruende fuer ein Token statt eines Passworts: es laesst sich einzeln
   widerrufen, ohne den Benutzer anzufassen, es umgeht keine
   Zwei-Faktor-Anmeldung, und es taucht in keinem Anmeldedialog auf.

   Der Wert wird **nur einmal** angezeigt (mit QR-Code). Sofort kopieren --
   danach ist er nicht mehr abrufbar und es muesste ein neues erzeugt werden.

7. **SSH-Add-on installieren.** Einstellungen → Add-ons → Add-on Store →
   *Advanced SSH & Web Terminal* (aus der Community-Sammlung; das offizielle
   "Terminal & SSH" geht auch). Noch nicht starten.

8. **Schluessel anlegen und eintragen:**

   ```
   python tools/ha.py keygen
   ```

   Der ausgegebene Public Key gehoert in die Add-on-Konfiguration unter
   `authorized_keys`. Ausserdem im Add-on:
   - `Protection mode` **aus** (sonst ist `/config` nicht sichtbar)
   - Port `22` freigeben
   - Add-on starten und *Beim Start starten* aktivieren

9. **`secrets.yaml` fuellen.** Vorlage ist `secrets.example.yaml`:

   ```yaml
   ha_host: "192.168.1.10"
   ha_token: "<das Token aus Schritt 6>"
   ha_ssh_key: "~/.ssh/ha_livingroom"
   ```

   Die Datei ist nicht im Git. Beim Ausrollen nach `/config/esphome` entfernt
   die Pipeline alle `ha_*`-Schluessel aus der uebertragenen Fassung -- das
   Token bleibt auf diesem Rechner.

10. **Zugang pruefen:**

    ```
    python tools/ha.py check
    ```

    Jede Zeile ist ein einzeln pruefbarer Punkt: Namensauflösung, Oberflaeche,
    SSH-Port, SSH-Anmeldung, Sicht auf `/config`, Token. Erst wenn hier alles
    steht, lohnt der naechste Schritt.

---

## Teil 2 -- Setup ausrollen

```
python tools/ha.py setup
```

Der Ablauf: Zugang pruefen → Dateien kopieren (inklusive `configuration.yaml`)
→ Home Assistant die Konfiguration pruefen lassen → neu starten → Dashboards
gegen die Registry verifizieren. Bricht ein Schritt ab, laufen die folgenden
nicht.

Danach den Controller einmalig verbinden: Einstellungen → Geraete & Dienste →
*Integration hinzufuegen* → ESPHome → Host `192.168.1.12`, Port `6053`,
Verschluesselungsschluessel = `wohnzimmer_api_key` aus `secrets.yaml`.

Zum Schluss:

```
python tools/ha.py verify
```

---

## Teil 3 -- Alltag

| Aufgabe | Befehl |
|---|---|
| Ist Home Assistant schon wieder da? | `python tools/ha.py wait` |
| Dashboard geaendert | `python tools/ha.py deploy` |
| `configuration.yaml` geaendert | `python tools/ha.py deploy --core` dann `restart` |
| ESPHome-Konfiguration auf den Host spiegeln | `python tools/ha.py deploy --esphome` |
| Vorher sehen, was passiert | `... deploy --dry-run` |
| Konfiguration pruefen | `python tools/ha.py validate` |
| Automatisierungen und Szenen neu laden | `python tools/ha.py reload` |
| Entitaeten fehlen in Home Assistant | `python tools/ha.py reload --integration` |
| Zustand aller Controller-Entitaeten | `python tools/ha.py entities --all` |
| Fehlerprotokoll | `python tools/ha.py logs --filter esphome` |

Geflasht wird weiterhin vom Arbeitsplatz aus (`esphome run livingroom.yaml
--device 192.168.1.12`). Die Kopie unter `/config/esphome` ist zum Nachschauen
und fuer Builds im Add-on, nicht der Arbeitsweg.

### Was der Deploy nicht anfasst

`automations.yaml`, `scripts.yaml` und `scenes.yaml` schreibt auch die
Oberflaeche. Sie werden nur mit `--core` ueberschrieben, damit ein normaler
Dashboard-Deploy nichts loescht, was in der Oberflaeche angelegt wurde. Vor
jedem Ueberschreiben landet der bisherige Stand auf dem Host unter
`/config/.deploy-backup/<zeitstempel>/`.

---

## Wenn Entitaeten in Home Assistant fehlen

Das war der haeufigste Aerger im alten Setup. Die zwei Ursachen lassen sich
sauber trennen:

```
python tools/verify_dashboard_entities.py   # liefert das GERAET die Entitaet?
python tools/ha.py verify                   # kennt HOME ASSISTANT sie?
```

- Erstes Skript meldet Fehlendes → die Firmware liefert die Entitaet nicht,
  Dashboard oder `livingroom.yaml` korrigieren.
- Nur das zweite meldet Fehlendes → Home Assistant hat sie nicht registriert.
  Zuerst `python tools/ha.py reload --integration`. Reicht das nicht, das Geraet
  in Home Assistant loeschen (Einstellungen → Geraete & Dienste → ESPHome →
  Geraet → Loeschen) und neu hinzufuegen. Dabei verschwinden auch alte
  Registry-Eintraege, die Namen blockieren und zu Entitaeten mit angehaengter
  `_2` fuehren. `python tools/ha.py entities` weist solche Dubletten aus.

Der Lift verliert dabei nichts: Position und Referenz liegen im NVS des ESP32
und ueberstehen sowohl Stromausfall als auch OTA.

---

## Was aus dem alten Setup absichtlich nicht mitkommt

- **`packages/livingroom_modes.yaml`.** Setzte Szenen, die es nicht mehr gibt,
  und triggerte auf einen entfernten Fault-Sensor. Die Raumszenen liegen auf
  dem ESP32 und funktionieren dort auch ohne Home Assistant. Die Pipeline
  loescht die Datei auf dem Host, nachdem sie sie gesichert hat.
- **`homeassistant: packages:`** in der `configuration.yaml`, weil es ohne das
  Package keinen Zweck hat.
- **Das top-level `mode: yaml`** unter `lovelace`. Es stellt die
  Standardansicht auf YAML um, woraufhin Home Assistant
  `/config/ui-lovelace.yaml` verlangt. Gemeint war immer nur das innere
  `mode: yaml` des zusaetzlichen Dashboards.
- **SMA und BLE.** Lief nicht und ist nicht uebernommen.
