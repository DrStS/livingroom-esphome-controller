# Funktions- & Test-Checkliste

Lebendes Dokument. Wir erweitern und testen Komponente für Komponente.

**Legende:**
`[x]` fertig + verifiziert · `[~]` implementiert, noch nicht getestet ·
`[ ]` geplant · `[HW]` wartet auf Hardware

---

## 1. System / Netzwerk
- [x] Ethernet W5500 (statische DHCP-Lease 192.168.1.12)
- [x] ESPHome-API + Verschlüsselung
- [x] OTA-Update
- [x] Controller-Restart-Button
- [x] Uptime-Sensor
- [x] IP-Adresse + MAC als Text-Sensor (Diagnose)
- [x] API-Connected Binary-Sensor
- [x] ESP-Temperatur + freier Heap + Loop-Zeit (Diagnose)

## 2. Strommessung (2× INA226)
- [x] 12V: Spannung / Strom / Leistung / Shunt
- [x] 5V: Spannung / Strom / Leistung / Shunt
- [ ] Energie kumuliert (kWh) pro Schiene (`total_daily_energy`)
- [ ] Überstrom-Warnung als Binary-Sensor (Schwelle je Schiene)
- [ ] Gesamtleistung (12V + 5V) als Template-Sensor

## 3. Temperatur / Feuchte
- [x] DHT22 Raum: Temperatur + Feuchte
- [x] DS18B20 AV-Receiver
- [x] DS18B20 Cabinet
- [ ] Taupunkt / absolute Feuchte (Template)
- [ ] Übertemperatur-Warnung je Zone (Binary-Sensor)

## 4. Lüfter (4-Pin)
- [x] PWM-Drehzahl über HA (`fan.av_fan`)
- [x] RPM-Sensor (Tacho)
- [ ] Stall-Erkennung (PWM > 0 aber RPM = 0 → Warnung)
- [ ] Automatik: Drehzahl nach AV-Receiver-Temperatur (Kurve)
- [ ] Min-Duty / Anlaufkick (damit Lüfter sicher anläuft)

## 5. LED-Strips (2× SK6812 RGBW)
- [x] Sideboard (GPIO45, RMT-DMA): An/Aus/Helligkeit/RGBW
- [x] Glass Edge (GPIO46, SPI-DMA): An/Aus/Helligkeit/RGBW
- [x] Effekte: Fireplace, Aurora, Lift Show, Fault, Rainbow, Color Wipe,
      Scan, Twinkle, Random Twinkle, Fireworks, Pulse, Pixel Walk
- [x] Szenen-Select + Intensity-Regler
- [~] „Effect Speed"-Regler (Entity existiert, wird noch nicht ausgewertet)
- [ ] Beide Strips synchron schalten (Gruppen-Schalter)
- [ ] Szenen um Speed-Kopplung erweitern

## 6. Motor / TV-Lift (IBT-2 / BTS7960)
- [x] Motor-Ausgänge RPWM/LPWM/Enable
- [x] „Motor Test Mode"-Schalter (5 s L / 5 s R, 50 %)
- [x] Motor beim Boot sicher stillgesetzt
- [x] TV-Lift-Cover (derzeit Mock/Simulation)
- [HW] Encoder-Positionszähler (GPIO16/18)
- [HW] Endschalter oben/unten als Binary-Sensor
- [HW] Referenzfahrt (Homing) gegen Endschalter
- [HW] Echte Positionsregelung (Cover ersetzt Mock)
- [HW] Sanftanlauf/Rampe hoch/runter
- [HW] Stall-/Blockier-Erkennung über 12V-Strom (INA226) → Fault + Stopp
- [HW] Soft-Limits (Position) zusätzlich zu Endschaltern

---

## Rollout-Reihenfolge (Vorschlag)
1. **System-Diagnose** (Uptime, IP, Link, ESP-Temp/Heap) — risikofrei, sofort.
2. **Energie + Überstrom** (INA226) — Basis für Motor-Stallschutz später.
3. **Lüfter-Automatik + Stall-Erkennung.**
4. **LED-Feinschliff** (Speed-Regler, Gruppenschalter).
5. **Motor/Lift** (sobald Motor + Encoder + Endschalter verbaut):
   Encoder → Endschalter → Homing → Positionsregelung → Stallschutz.

Jede Stufe: implementieren → `esphome run` → in HA testen → hier abhaken → commit.
