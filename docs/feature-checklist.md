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
- [~] Onboard-RGB-LED (GPIO21) per Firmware auf AUS (1-px-Strip, `ALWAYS_OFF`) —
      Verhalten beim Trennen von 12V/5V noch zu bestätigen

## 2. Strommessung (2× INA226)
- [x] 12V: Spannung / Strom / Leistung / Shunt
- [x] 5V: Spannung / Strom / Leistung / Shunt
- [~] Energie kumuliert (Wh) pro Schiene (`total_daily_energy`, HA-Zeitquelle) — in HA prüfen
- [~] Überstrom-Warnung als Binary-Sensor (Schwelle je Schiene, `config/hardware.yaml`) — Schwellen an reale Last anpassen
- [~] Gesamtleistung (12V + 5V) als Template-Sensor (`Rail Total Power`) — in HA prüfen

## 3. Temperatur / Feuchte
- [x] DHT22 Raum: Temperatur + Feuchte
- [x] DS18B20 AV-Receiver
- [x] DS18B20 Cabinet
- [ ] Taupunkt / absolute Feuchte (Template)
- [ ] Übertemperatur-Warnung je Zone (Binary-Sensor)

## 4. Lüfter (4-Pin)
- [x] RPM-Sensor (Tacho, GPIO38 — von GPIO21 verlegt wg. Onboard-LED-Konflikt)
- [x] Automatik: lineare Kennlinie nach AV-Receiver-Temperatur (geschlossener Regelkreis)
      ≤ 26 C → 500 rpm · 26–34 C → linear 500→1400 rpm · > 34 C → Vollgas (0,5 C Hysterese)
- [x] Anlauf-Kick + Min-Duty (Lüfter läuft sicher an)
- [x] Duty als Diagnose-Sensor (`AV Fan Duty`)
- [x] Kein manueller Regler mehr (rein automatisch)
- [x] Leerlauf verifiziert: 500 rpm @ ~24 C, Duty ~35 %, stabil
- [ ] Rampe (26–34 C) und Vollgas-Übergang (> 34 C) per Anwärmen live verifizieren
- [ ] Stall-Erkennung (Duty > 0 aber RPM = 0 → Warn-Binary-Sensor)

## 5. LED-Strips (2× SK6812 RGBW)
- Sideboard: **111 px**, GPIO45, **SPI-DMA** (kurzes Kabel). Cabinet Glass Edge:
  **102 px** (6 Zonen), GPIO46, **RMT-DMA** (langes Kabel).
- [x] Beide Strips angeschlossen + An/Aus/Helligkeit/RGBW verifiziert (nicht vertauscht)
- [x] Beide Strips flackerfrei (statisches Weiss unter W5500-Last bestaetigt)
- [x] Treiberzuordnung nach Kabellaenge geloest (per Treiber-Tausch verifiziert):
      Sideboard (kurzes Kabel) = SPI-DMA, Glass Edge (langes Kabel) = RMT-DMA.
- [x] Pixelzahlen per Length/Zone-Check verifiziert: Sideboard 111, Cabinet 102
- [x] Cabinet in 6 Zonen verifiziert (Zone-Check): 3× (25 Kante + 9 Spot),
      Reihenfolge unten → Mitte → oben
- [x] LED-Wiring-Test-Button (R→G→B→W + Pixel Walk)
- [x] Effektlisten getrennt: `config/effects_sideboard.yaml` (Basis) und
      `config/effects_cabinet.yaml` (Basis + Zonen). Kamineffekt als externe
      C++-Komponente `components/fireplace_effect/` (roher PWM-Pfad)
- [~] Zonen-Effekt Cabinet: **Cabinet Museum** (implementiert, visuell abzunehmen)
- [~] Kamineffekt „Fireplace" / „Fireplace Palette" (RGBW-Rohpalette über
      RawPixelOutput, DMA-serialisiert) — visuell abzunehmen; Cabinet
      `output_gain: 50%` als Startwert
- [!] RISIKO: Cabinet (serialisierter RMT) kann die obersten ~30 LEDs nicht
      ansteuern / bei paralleler Sideboard-DMA-Last aufblitzen. Bekanntes
      DMA-Kollisionsthema, mit voller Bundle-Integration (Option C) akzeptiert.
- [~] Szenen-Select + Intensity-Regler (implementiert, noch nicht in HA getestet)
- [ ] Effekte animiert unter Dauerlast auf beiden Strips gleichzeitig glitchfrei
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
