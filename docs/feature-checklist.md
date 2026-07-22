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
- [x] RPM-Sensor (Tacho)
- [x] Automatik: lineare Kennlinie nach AV-Receiver-Temperatur (geschlossener Regelkreis)
      ≤ 26 C → 500 rpm · 26–34 C → linear 500→1400 rpm · > 34 C → Vollgas (0,5 C Hysterese)
- [x] Anlauf-Kick + Min-Duty (Lüfter läuft sicher an)
- [x] Duty als Diagnose-Sensor (`AV Fan Duty`)
- [x] Kein manueller Regler mehr (rein automatisch)
- [x] Leerlauf verifiziert: 500 rpm @ ~24 C, Duty ~35 %, stabil
- [ ] Rampe (26–34 C) und Vollgas-Übergang (> 34 C) per Anwärmen live verifizieren
- [ ] Stall-Erkennung (Duty > 0 aber RPM = 0 → Warn-Binary-Sensor)

## 5. LED-Strips (2× SK6812 RGBW)
- [x] Sideboard (GPIO45, RMT-DMA): An/Aus/Helligkeit/RGBW (Bring-up statisch verifiziert)
- [x] Glass Edge (GPIO46, SPI-DMA): An/Aus/Helligkeit/RGBW (Bring-up statisch verifiziert)
- [x] Beide Strips flackerfrei (statisches Weiss unter W5500-Last bestaetigt)
- [x] Beide Strips angeschlossen + An/Aus verifiziert (nicht vertauscht)
- [x] Treiberzuordnung nach Kabellaenge geloest (per Treiber-Tausch verifiziert):
      Sideboard (GPIO45, kurzes Kabel) = SPI-DMA, Glass Edge (GPIO46, langes Kabel)
      = RMT-DMA. Beide flackerfrei + glitchfrei, auch mit langem Kabel am Cabinet.
- [x] LED-Wiring-Test-Button (R→G→B→W + Pixel Walk automatisch, fuer Farbreihenfolge/Anzahl/tote Pixel)
- [x] Effekte animiert auf beiden Strips glitchfrei (Rainbow/Fireworks/etc.
      beim Treiber-Tausch-Test bestaetigt): Fireplace, Aurora, Lift Show, Fault,
      Rainbow, Color Wipe, Scan, Twinkle, Random Twinkle, Fireworks, Pulse, Pixel Walk
- [~] Echter Feuer-Effekt „Fireplace" = Fire2012 (Kriegsman) mit Schwarzkoerper-
      Farbabbildung, pro-Pixel-Waerme (effect_data), Regler „Fire Cooling" +
      „Fire Sparking" (implementiert, visuell noch zu bewerten)
- [~] Szenen-Select + Intensity-Regler (implementiert, noch nicht in HA getestet)
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
