# Plan: 3 getrennte Lichter (Sideboard, Vitrine Glass, Vitrine Spots)

**Version:** v1.1.0 (geplant)
**Vorbedingung:** v1.0.1 stabil, 6× Reboot getestet

## Motivation

Die Cabinet Glass Edge besteht physisch aus 102 Pixeln in 6 Zonen:
- 3× Kante (je 25 px): Pixelpositionen 0–24, 34–58, 68–92
- 3× Spot (je 9 px): Pixelpositionen 25–33, 59–67, 93–101

Aktuell ist das ein einziges Licht mit einem Helligkeitsregler. Ziel:
Kanten und Spots getrennt in der Intensität steuerbar, bei allen Effekten
und manuellen Farben.

## Zielzustand

| Licht | Entity-ID | Pixel | Effekte |
|---|---|---|---|
| Sideboard | `light_sideboard` | 111 px | Cinema, Fireplace, Fireworks |
| Vitrine Glass | `light_vitrine_glass` | 75 px (3×25 Kanten) | Museum-Kante, Fireplace, Fireworks |
| Vitrine Spots | `light_vitrine_spots` | 27 px (3×9 Spots) | Museum-Spot, Fireplace, Fireworks |

## Technischer Ansatz

### Option A: ESPHome `light_partition` (bevorzugt)

```yaml
# Physischer Strip bleibt internal:
- platform: serialized_rmt_led
  id: cabinet_physical
  internal: true
  ...

# Zwei Partitionen:
- platform: partition
  name: "Vitrine Glass"
  id: light_vitrine_glass
  segments:
    - id: cabinet_physical
      from: 0
      to: 24
    - id: cabinet_physical
      from: 34
      to: 58
    - id: cabinet_physical
      from: 68
      to: 92
  effects: [Museum-Kante, Fireplace, Fireworks]

- platform: partition
  name: "Vitrine Spots"
  id: light_vitrine_spots
  segments:
    - id: cabinet_physical
      from: 25
      to: 33
    - id: cabinet_physical
      from: 59
      to: 67
    - id: cabinet_physical
      from: 93
      to: 101
  effects: [Museum-Spot, Fireplace, Fireworks]
```

**Zu prüfen:** Unterstützt `serialized_rmt_led` das AddressableLightOutput-
Interface, das `partition` verlangt? Falls nicht → Option B.

### Option B: Ein Licht + zwei Globals als Zonenintensität

Einfacher, keine Partition nötig. Zwei Number-Slider (0–100 %) als
Globals, Multiplikator im Render-Code. In HA nur Slider, keine
eigenständigen Lichter mit Farbwähler.

## Szenen-Mapping

| Szene | Sideboard | Vitrine Glass | Vitrine Spots |
|---|---|---|---|
| Manual | unberührt | unberührt | unberührt |
| Aus | aus | aus | aus |
| Kaminfeuer | Fireplace | Fireplace | Fireplace |
| Vitrine | aus | Museum-Kante | Museum-Spot |
| Kaminfeuer + Vitrine | Fireplace | Museum-Kante | Museum-Spot |
| Cinema | Cinema | Museum-Kante | Museum-Spot |
| Feuerwerk | Fireworks | Fireworks | Fireworks |

## Betroffene Dateien

| Datei | Änderung |
|---|---|
| `livingroom.yaml` – light-Block | 1 serialized_rmt_led → internal + 2 partition |
| `livingroom.yaml` – `apply_light_scene` | Cabinet-Referenzen → 2 neue IDs |
| `livingroom.yaml` – LED-Wiring-Test | Cabinet-Referenzen anpassen |
| `config/effects_cabinet.yaml` | Museum in 2 Varianten (Kante/Spot) |
| HA `scripts.yaml` | 3 Lichter merken/wiederherstellen |
| HA `configuration.yaml` | +1 input_text, +1 input_number |
| HA `dashboards/livingroom.yaml` | 3 Licht-Tiles |

## Risiken

1. `serialized_rmt_led` muss das partition-Interface unterstützen
2. Frame-Timing bei zwei Partitionen auf einem Strip (Token-System prüfen)
3. Drei NVS-Einträge statt zwei (mehr Schreibvorgänge pro Sync)
4. Bootschleife wenn Partition-Lichter beim Setup crashen → USB-Rettung

## Geschätzter Aufwand

~2,5 Stunden in einer frischen Session.

## Testplan

1. Kompilieren ohne Fehler
2. OTA flashen
3. Jedes Licht einzeln ein/aus/Farbe
4. Jede Szene durchschalten
5. Kino-Modus Toggle (merkt 3 Lichter)
6. Gute-Nacht-Modus Toggle
7. 3× Reboot: Position, Referenz, alle 3 Lichter korrekt wiederhergestellt
