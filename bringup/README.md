# Bring-up-Firmwares (Archiv)

Eigenstaendige, minimale ESPHome-Konfigurationen aus der Inbetriebnahme des
Encoder- und Motorpfads. Sie sind hier nur zur Nachvollziehbarkeit archiviert.

## WARNUNG

Das sind **vollstaendige Firmwares**, nicht Ergaenzungen. Ein Flash mit einer
dieser Dateien ersetzt die Produktionsfirmware `livingroom.yaml` und damit
Positionsregelung, Softlimits, Referenz und Watchdogs. Die Dateien steuern die
Motorausgaenge teilweise direkt und ohne Encoderueberwachung.

Nicht am eingebauten Lift verwenden.

| Datei | Zweck damals |
|---|---|
| `encoder_1ms_poll.yaml` | GPIO16/GPIO18 alle 1 ms pollen und Flanken zaehlen, ohne jeden Treiber. Damit wurde bewiesen, dass beide Hall-Kanaele Signale liefern. |
| `encoder_pins_basic.yaml` | Zwei eingebaute `pulse_counter` auf den Encoderpins. |
| `encoder_rotary_basic.yaml` | ESPHome-`rotary_encoder` als Vergleichspfad. |
| `encoder_test.yaml` | Fruehe Encoder-Testkonfiguration. |
| `pcnt_encoder_test.yaml` | Isolierter Test der eigenen `pcnt_quadrature`-Komponente mit zeitlich begrenzten Motorimpulsen. |

## Ergebnis der Inbetriebnahme

Der Encoder liefert **12 Counts pro Motorumdrehung** (3 Signalzyklen je Kanal,
x4-Quadratur). Bestaetigt durch Flankenvergleich: PCNT-Delta 496 gegen
Flankensumme beider Kanaele 494. Die Datenblattangabe "6 signals per rotation"
bezeichnet die Flanken, nicht die Zyklen.

Die produktive Diagnose laeuft heute ueber die Firmware selbst und die Skripte
in `tools/`; diese Bring-up-Firmwares werden nicht mehr gebraucht.
