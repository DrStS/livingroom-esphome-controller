# Funktions- & Test-Checkliste

Lebendes Dokument. Wir erweitern und testen Komponente für Komponente.

**Legende:**
`[x]` fertig + verifiziert · `[~]` implementiert, noch nicht abgenommen ·
`[ ]` geplant · `[!]` bekanntes Risiko

---

## 1. System / Netzwerk
- [x] Ethernet W5500 (statische DHCP-Lease 192.168.1.12)
- [x] ESPHome-API + Verschlüsselung
- [x] OTA-Update (stoppt vorher den Lift)
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
- [x] Energie kumuliert (Wh) pro Schiene (`total_daily_energy`, HA-Zeitquelle)
- [x] Gesamtleistung (12V + 5V) als Template-Sensor (`Rail Total Power`)
- [~] Überstrom-Warnung als Binary-Sensor je Schiene (`config/hardware.yaml`) —
      Schwellen noch konservative Platzhalter, an reale Last anpassen

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
- [x] Kein manueller Regler (rein automatisch)
- [x] Leerlauf verifiziert: 500 rpm @ ~24 C, Duty ~35 %, stabil
- [ ] Rampe (26–34 C) und Vollgas-Übergang (> 34 C) per Anwärmen live verifizieren
- [ ] Stall-Erkennung (Duty > 0 aber RPM = 0 → Warn-Binary-Sensor)

## 5. LED-Strips (2× SK6812 RGBW)
- Sideboard: **111 px**, GPIO45, **SPI-DMA** (kurzes Kabel). Cabinet Glass Edge:
  **102 px** (6 Zonen), GPIO46, **RMT-DMA** (langes Kabel).
- [x] Beide Strips angeschlossen + An/Aus/Helligkeit/RGBW verifiziert (nicht vertauscht)
- [x] Beide Strips flackerfrei (statisches Weiss unter W5500-Last bestaetigt)
- [x] Treiberzuordnung nach Kabellaenge geloest (per Treiber-Tausch verifiziert):
      Sideboard (kurzes Kabel) = SPI-DMA, Glass Edge (langes Kabel) = RMT-DMA
- [x] Pixelzahlen per Length/Zone-Check verifiziert: Sideboard 111, Cabinet 102
- [x] Cabinet in 6 Zonen verifiziert (Zone-Check): 3× (25 Kante + 9 Spot),
      Reihenfolge unten → Mitte → oben
- [x] Effektlisten getrennt: `config/effects_sideboard.yaml` und
      `config/effects_cabinet.yaml`. Kamineffekt als externe C++-Komponente
      `components/fireplace_effect/` (roher PWM-Pfad)
- [x] Effektlisten am Geraet abgefragt (`tools/check_light_effects.py`):
      Sideboard = None / Fireplace / Cinema / Fireplace Palette (Kalibrierung) /
      Fireworks · Cabinet = None / Fireplace / Fireplace Palette (Kalibrierung) /
      Museum / Fireworks
- [x] Szenen-Select mit 7 Optionen am Geraet verifiziert: Manual, Aus,
      Kaminfeuer, Vitrine, Kaminfeuer + Vitrine, Cinema, Feuerwerk
- [x] Cinema (nur Sideboard): statisches Parabelprofil g = (1 − d²)² über 111 px,
      bewusst ohne Flackern, weil der Strip direkt unter dem Bildschirm sitzt.
      Parameter als `cinema_*` in `config/hardware.yaml`
- [x] Szene und Intensitaet ueberleben Neustart (`restore_value`), Einzelzustaende
      der Lichter per `restore_mode`
- [~] Zonen-Effekt Cabinet **Museum** (frueher „Cabinet Museum") — visuell abzunehmen
- [~] Kamineffekt „Fireplace" / „Fireplace Palette (Kalibrierung)" (RGBW-Rohpalette
      über RawPixelOutput, DMA-serialisiert) — visuell abzunehmen; Cabinet
      `output_gain: 50%` als Startwert
- [~] LED-Wiring-Test als Firmware-Skript `led_wiring_test` (R→G→B→W + Pixel Walk) —
      derzeit nicht als Home-Assistant-Entität ausgespielt
- [!] RISIKO: Cabinet (serialisierter RMT) kann die obersten ~30 LEDs nicht
      ansteuern / bei paralleler Sideboard-DMA-Last aufblitzen. Bekanntes
      DMA-Kollisionsthema, mit voller Bundle-Integration akzeptiert.
- [ ] Effekte animiert unter Dauerlast auf beiden Strips gleichzeitig glitchfrei
- [ ] Beide Strips synchron schalten (Gruppen-Schalter)

## 6. Motor / TV-Lift (IBT-2 / BTS7960)
- [x] Motor-Ausgänge: Enable GPIO39, RPWM GPIO40 (abwärts), LPWM GPIO41 (aufwärts)
- [x] Motor beim Boot sicher stillgesetzt
- [x] Encoder-Positionszähler GPIO16 (A) / GPIO18 (B), reine Eingänge ohne interne
      Pull-ups, PCNT x4, `internal_filter: 10us`, `invert_direction: true`
      (aufwärts zählt positiv)
- [x] Encoderauflösung nachgewiesen: **12 Counts pro Motorumdrehung**
      (3 Signalzyklen je Kanal, x4). Die Datenblattangabe „6 signals per rotation"
      meint Flanken, nicht Zyklen — Beleg: PCNT-Delta 496 vs. Flankensumme
      A+B 494
- [x] Umrechnung aus der Übersetzung: Getriebe 48:1, Steigung 4 mm →
      576 Counts/Spindelumdrehung → **144 Counts/mm** (1 Count = 6,94 µm)
- [x] Hub gemessen: **94500 Counts = 656,25 mm**, Softlimits 0..94500
- [x] Positionsregelung: eigener FreeRTOS-Task auf Kern 1, 1 ms Zyklus,
      Trapezprofil + PI-Regler auf die gemessene Encodergeschwindigkeit
      (lastunabhängig)
- [x] Fahrprofil: `max_speed` 1500 counts/s (~10,4 mm/s, voller Hub ca. 63 s),
      `accel` 300, `decel` 500, `approach_speed` 150 counts/s
- [x] Marschgeschwindigkeit zur Laufzeit verstellbar
      (`number.…_lift_max_geschwindigkeit`)
- [x] Cover fährt echt: Pfeile = manuelle Fahrt, Positions-Slider = Zielfahrt
      (nur nach Referenzierung)
- [x] Referenzieren über **einen** Schalter (`switch.…_lift_referenz`):
      EIN = untere Endlage erreicht → Position 0, AUS = Referenz verwerfen
- [x] Sprechende Lageanzeige als Text-Sensor: Unten / Oben / Zwischenlage xx % /
      Faehrt hoch / Faehrt runter / Nicht referenziert
- [x] Reglerzustand als Diagnose-Text-Sensor: IDLE / MANUAL / MOVING / REACHED /
      STALL / TIMEOUT / LIMIT / FAULT / UNREFERENCED
- [x] Geschwindigkeit (counts/s) und Positionen in Pulsen/mm/Prozent als Sensoren
- [x] Softlimits doppelt geprüft (Mainloop und Regeltask)
- [x] Stall-Abschaltung nach 800 ms ohne Encoderbewegung
- [x] Timeout 90 s für Positionsfahrt und manuelle Fahrt
- [x] Gegenseitige Heartbeat-Überwachung Mainloop ↔ Regeltask
- [x] OTA stoppt den Lift vor dem Update
- [x] Persistenz im NVS: Position und Referenz, geschrieben nur bei Fahrtbeginn,
      Fahrtende und Referenzereignissen (zwei Schreibvorgänge pro Fahrt, mit
      sofortigem sync)
- [x] Nach Spannungsverlust während einer Fahrt startet der Lift bewusst
      unreferenziert
- Endschalter sind bewusst **nicht** in der Firmware: die Endlagenabschaltung ist
  hardwareseitig auf der Power-Seite gelöst. In Software schützen Softlimits,
  Stall-Abschaltung und Timeout.
- [x] PI-Parameter sind konservative Startwerte — Feinabstimmung am eingebauten
      Lift noch offen
- [ ] Stall-/Blockier-Erkennung zusätzlich über 12V-Strom (INA226)

---

## Was als Nächstes offen ist
1. **Optische Abnahme der Effekte** (Museum, Fireplace, Cinema) am eingebauten Möbel.
2. **Überstromschwellen** je Schiene an die reale Last anpassen.
3. **Lüfter-Rampe** durch Anwärmen des AV-Receivers verifizieren.
4. **PI-Feinabstimmung** des Lifts und Bewertung des Auslaufs an beiden Endlagen.
5. **Doppelter Stallschutz** über den 12V-Strom.

Jede Stufe: implementieren → `esphome run` → am Gerät prüfen (`tools/`) → hier
abhaken → commit.
