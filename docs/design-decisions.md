# Design Decisions

## Warum GPIOs in YAML und nicht fest im C++?

Die GPIO-Zuordnung ist Hardwarekonfiguration, nicht Produktlogik.

### Vorteile

1. **Board-Varianten ohne C++-Änderung**
   - Mock
   - Prototyp-Lochraster
   - finale Hauptplatine

2. **Core bleibt testbar**
   - `LiftController` kennt keine ESP32-Pins.
   - Unit-Tests laufen mit CMake lokal ohne ESPHome.

3. **ESPHome-Codegen macht Pinobjekte**
   - YAML validiert Pins.
   - Der Adapter bekommt `GPIOPin*`.
   - Der Core bekommt nur Events/Zustände.

4. **SickiSense-Schnitt**
   - Hardware Mapping außen.
   - Deterministische Logik innen.
   - Keine globalen Pin-Makros im Core.

## Grenze

Zeitkritische Logik gehört nicht in YAML-Lambdas. Deshalb:
- Pins in YAML.
- Motion/Safety/Effects in C++20.
- ESPHome Adapter verbindet beides.

## Hardware-Constraints (Board-spezifisch)

Die konkrete Pinbelegung, reservierte Pins (Octal-PSRAM GPIO33–37, Ethernet
GPIO9–14, Strapping GPIO0/3/45/46) und der aktuelle Sensor-Status (echt vs. Mock)
sind in **`docs/hardware-pinmap.md`** dokumentiert.

Wichtig für die YAML-außen/C++-innen-Entscheidung: Genau weil die Pinbelegung
board-abhängig ist (und z. B. Octal-PSRAM ganze GPIO-Bereiche sperrt), bleibt die
Zuordnung in `config/pins.yaml` und nicht im C++-Core.
