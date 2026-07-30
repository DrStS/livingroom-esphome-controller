# Hardware & Pinbelegung

Board: **Waveshare ESP32-S3-ETH** (WROOM-1-N8R8, 8 MB Octal-PSRAM), Ethernet W5500.
Firmware: `livingroom.yaml` (Gerätename `wohnzimmer-controller`).
Pin-Quelle der Wahrheit: `config/pins.yaml`, Kennwerte: `config/hardware.yaml`.

## Verdrahtung (was ist angeschlossen)

| GPIO | Funktion | Bauteil |
|---|---|---|
| GPIO13 | ETH CLK | W5500 |
| GPIO11 | ETH MOSI | W5500 |
| GPIO12 | ETH MISO | W5500 |
| GPIO14 | ETH CS | W5500 |
| GPIO10 | ETH INT | W5500 |
| GPIO9  | ETH RST | W5500 |
| GPIO0  | Temp + Feuchte | DHT22 (Strapping-Pin, interner Pull-up aktiv) |
| GPIO1  | AV Receiver Temp | DS18B20 (eigener 1-Wire-Bus) |
| GPIO2  | Cabinet Temp | DS18B20 (eigener 1-Wire-Bus) |
| GPIO48 | I2C SDA | 2× INA226 (0x40 = 5V, 0x41 = 12V) |
| GPIO47 | I2C SCL | 2× INA226 |
| GPIO17 | Lüfter PWM | 4-Pin-Fan (LEDC 25 kHz) |
| GPIO38 | Lüfter Tacho | 4-Pin-Fan (pulse_counter; war GPIO21, wg. Onboard-LED verlegt) |
| GPIO21 | Onboard-RGB-LED | Waveshare WS2812 — per Firmware auf AUS gesetzt |
| GPIO45 | LED Sideboard | SK6812 RGBW, 111 px, über 74AHCT125 (SPI-DMA, MOSI SPI3, kurzes Kabel) |
| GPIO46 | LED Cabinet Glass Edge | SK6812 RGBW, 102 px (6 Zonen), über 74AHCT125 (RMT-DMA, langes Kabel) |
| GPIO39 | Motor Enable | IBT-2 (R_EN + L_EN gebrückt) |
| GPIO40 | Motor RPWM | IBT-2 / BTS7960 — **abwärts** |
| GPIO41 | Motor LPWM | IBT-2 / BTS7960 — **aufwärts / öffnen** |
| GPIO16 | Encoder A | Hall-Quadratur, reiner Eingang **ohne** internen Pull-up |
| GPIO18 | Encoder B | Hall-Quadratur, reiner Eingang **ohne** internen Pull-up |

Endschalter sind bewusst **nicht** in der Firmware. Die Endlagenabschaltung ist
hardwareseitig auf der Power-Seite gelöst; in der Software schützen Softlimits,
Stall-Abschaltung und Timeout. GPIO19/20 bleiben für natives USB frei.

## Antriebsstrang (verifiziert)

**Encoder.** Hall-Geber auf der Motorwelle, **12 Counts pro Motorumdrehung**:
3 Signalzyklen je Kanal, x4-Quadratur. Die Datenblattangabe „6 signals per
rotation" bezeichnet die **Flanken** je Kanal, nicht die Zyklen. Nachgewiesen
durch Flankenvergleich: PCNT-Delta 496 gegen Flankensumme A+B 494.

Auswertung über die eigene Komponente `components/pcnt_quadrature`
(PCNT-Hardwarezähler, `resolution: 4`, `internal_filter: 10us`,
`invert_direction: true`, damit aufwärts positiv zählt).

**Umrechnung.** Aus der Übersetzung abgeleitet, nicht aus einer geschätzten
Hublänge:

| Größe | Wert |
|---|---|
| Counts/Motorumdrehung | 12 |
| Getriebeuntersetzung | 48:1 |
| Counts/Spindelumdrehung | 12 × 48 = 576 |
| Spindelsteigung | 4 mm/Umdrehung (TR16x4) |
| Counts/mm | 576 / 4 = 144 |
| 1 Count | 6,94 µm |
| Hub | 94500 Counts = 656,25 mm |
| Softlimits | 0 … 94500 Counts |

**Regelung.** Eigener FreeRTOS-Task auf Kern 1 mit 1 ms Zyklus
(`components/lift_motor`). Trapezprofil plus PI-Regler auf die **gemessene**
Encodergeschwindigkeit, dadurch lastunabhängig. Duty ist nur noch Stellgröße.

| Parameter | Wert |
|---|---|
| `max_speed` | 1500 counts/s (~10,4 mm/s, voller Hub ca. 63 s) |
| `accel` | 300 counts/s² |
| `decel` | 500 counts/s² |
| `approach_speed` | 150 counts/s |

Zur Laufzeit ist die Marschgeschwindigkeit über
`number.wohnzimmer_controller_lift_max_geschwindigkeit` verstellbar (z. B. klein
für Messfahrten).

**Sicherheit.**
- Softlimits doppelt geprüft (Mainloop und Regeltask).
- Stall-Abschaltung nach 800 ms ohne Encoderbewegung bei Sollgeschwindigkeit > 0.
- Timeout 90 s, sowohl für Positionsfahrt als auch für manuelle Pfeilfahrt.
- Gegenseitige Heartbeat-Überwachung zwischen Mainloop und Regeltask.
- Kein Motorlauf beim Boot.
- OTA stoppt den Lift, bevor das Update beginnt.

**Persistenz.** Position und Referenz liegen im NVS. Geschrieben wird nur bei
Fahrtbeginn, Fahrtende und Referenzereignissen — zwei Schreibvorgänge pro Fahrt,
jeweils mit sofortigem sync. Wird eine Fahrt durch Spannungsverlust
unterbrochen, startet der Lift bewusst **unreferenziert**; dann über den
Schalter `Lift Referenz` in der unteren Endlage neu referenzieren.

## Cabinet Glass Edge — 6 Zonen (102 px, verifiziert per Zone-Check)

Reihenfolge entlang des Streifens (unten → Mitte → oben):

| Zone | LED-Index | Länge | Funktion |
|---|---|---|---|
| Kante Unten | 0–24 | 25 | Glaskante von hinten beleuchtet |
| Spot Unten | 25–33 | 9 | 45°-Strahler auf den Fachinhalt |
| Kante Mitte | 34–58 | 25 | Glaskante |
| Spot Mitte | 59–67 | 9 | 45°-Strahler |
| Kante Oben | 68–92 | 25 | Glaskante |
| Spot Oben | 93–101 | 9 | 45°-Strahler |

## Effekte je Strip

Effektlisten sind getrennt (`config/effects_sideboard.yaml` /
`config/effects_cabinet.yaml`).

| Strip | Effekte |
|---|---|
| Sideboard | `None`, `Fireplace`, `Cinema`, `Fireplace Palette (Kalibrierung)`, `Fireworks` |
| Cabinet | `None`, `Fireplace`, `Fireplace Palette (Kalibrierung)`, `Museum`, `Fireworks` |

- **Museum** (nur Cabinet): Spots warmweiß, Kanten als warmweißer
  Parabel-Verlauf. Hieß früher „Cabinet Museum".
- **Cinema** (nur Sideboard): statisches Parabelprofil g = (1 − d²)² über alle
  111 Pixel, bewusst **ohne** Flackern, weil der Strip direkt unter dem
  Bildschirm sitzt. Parameter als `cinema_*`-Substitutions in
  `config/hardware.yaml`.
- **Fireplace** / **Fireplace Palette (Kalibrierung)**: externe C++-Komponente
  `components/fireplace_effect/` über den rohen PWM-Pfad (`RawPixelOutput`).

## Kurz-Notizen (nur das Nötigste)

- **Nicht nutzbar:** GPIO26–32 (Flash), GPIO33–37 (Octal-PSRAM), GPIO9–14 (ETH),
  GPIO19/20 (native USB — bleibt für USB).
- **LEDs:** 2 Strips, je eigener DMA-Weg (interrupt-immun trotz W5500). Der S3 hat
  nur **einen** DMA-RMT-Kanal. Zuordnung nach Kabellänge optimiert:
  **Sideboard (GPIO45, kurzes Kabel) = SPI-DMA** (eigener Treiber
  `components/spi_clockless_led`, MOSI auf SPI3), **Glass Edge (GPIO46, langes
  Kabel) = RMT-DMA**. RMT hat über die lange Leitung mehr Signalreserve; SPI über
  lange Kabel glitcht (per Treiber-Tausch verifiziert). Non-DMA flimmert und ist
  bewusst nicht im Einsatz.
- **Lüfter:** automatische Temperatur-Regelung nach AV-Receiver-Temp (kein
  manueller Regler). Tacho auf GPIO38, `AV Fan Duty` als Diagnose-Sensor.
- **Entity-IDs** in Home Assistant tragen das Präfix `wohnzimmer_controller_`,
  vollständige Liste in `docs/entity-map.md`.
