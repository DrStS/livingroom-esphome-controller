# Hardware & Pinbelegung

Board: **Waveshare ESP32-S3-ETH** (WROOM-1-N8R8, 8 MB Octal-PSRAM), Ethernet W5500.
Firmware: `livingroom.yaml` (Gerätename `wohnzimmer-controller`).
Pin-Quelle der Wahrheit: `config/pins.yaml`.

## Verdrahtung (was ist angeschlossen)

| GPIO | Funktion | Bauteil |
|---|---|---|
| GPIO13 | ETH CLK | W5500 |
| GPIO11 | ETH MOSI | W5500 |
| GPIO12 | ETH MISO | W5500 |
| GPIO14 | ETH CS | W5500 |
| GPIO10 | ETH INT | W5500 |
| GPIO9  | ETH RST | W5500 |
| GPIO0  | Temp + Feuchte | DHT22 |
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
| GPIO40 | Motor RPWM | IBT-2 / BTS7960 |
| GPIO41 | Motor LPWM | IBT-2 / BTS7960 |
| GPIO16 | Encoder A | Quadratur-Encoder |
| GPIO18 | Encoder B | Quadratur-Encoder |

Reserviert, noch nicht verbunden: Endschalter oben (`pin_endstop_top` = GPIO3,
Strapping-Pin) und unten (`pin_endstop_bottom` = GPIO44). GPIO38 ist jetzt der
Lüfter-Tacho (nicht mehr als Endschalter verfügbar). Die Endschalter-Pins sind
nach dem Tacho-Umbau noch final zu verifizieren, sobald die Mechanik verbaut ist.

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

Zonenbasierter Effekt (nur Cabinet): **Cabinet Museum** (Spots warmweiß + Kanten
als warmweißer Parabel-Verlauf, Reveal von unten nach oben beim Start).
Effektlisten sind getrennt (`config/effects_sideboard.yaml` /
`config/effects_cabinet.yaml`). Der Kamineffekt (**Fireplace** / **Fireplace
Palette**) ist keine Lambda-Datei mehr, sondern die externe C++-Komponente
`components/fireplace_effect/` (roher PWM-Pfad über `RawPixelOutput`).

## Kurz-Notizen (nur das Nötigste)

- **Nicht nutzbar:** GPIO26–32 (Flash), GPIO33–37 (Octal-PSRAM), GPIO9–14 (ETH),
  GPIO19/20 (native USB — bleibt für USB).
- **LEDs:** 2 Strips, je eigener DMA-Weg (interrupt-immun trotz W5500). Der S3 hat
  nur **einen** DMA-RMT-Kanal. Zuordnung nach Kabellänge optimiert:
  **Sideboard (GPIO45, kurzes Kabel) = SPI-DMA** (eigener Treiber
  `components/spi_clockless_led`, MOSI auf SPI3), **Glass Edge (GPIO46, langes
  Kabel) = RMT-DMA** (`esp32_rmt_led_strip`). RMT hat über die lange Leitung mehr
  Signalreserve; SPI über lange Kabel glitcht (per Treiber-Tausch verifiziert).
  Non-DMA flimmert und ist bewusst nicht im Einsatz.
- **Lüfter:** automatische Temperatur-Regelung nach AV-Receiver-Temp (kein
  manueller Regler mehr). Tacho auf GPIO38, `AV Fan Duty` als Diagnose-Sensor.
- **Motor-Test:** Schalter `Motor Test Mode` (Standard AUS) fährt zum Bring-up
  5 s links / 5 s rechts bei 50 % Duty. Kein Endschalter-Schutz — nur bei
  freilaufender Mechanik einschalten.
- **Entity-IDs** in Home Assistant tragen das Präfix `wohnzimmer_controller_`.
