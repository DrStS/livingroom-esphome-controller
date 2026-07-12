# Verdrahtung / Hauptplatine

## Rails

```text
RSP-200-12 -> 12V Rail -> Motortreiber + Lüfter
RSP-100-5  -> 5V Rail  -> LED-Zonen + 74AHCT125 VCC
PoE        -> ESP32 Logik
```

GND-Sternpunkt auf der Niedervolt-Hauptplatine:
- 12V-GND
- 5V-GND
- ESP32-GND
- Motortreiber-GND
- LED-GND
- Sensor-GND

## LED-Levelshifter

```text
ESP32 GPIO -> 74AHCT125 A
74AHCT125 Y -> 330R -> LED DATA
74AHCT125 VCC -> +5V
74AHCT125 GND -> GND
/OE -> GND
```

## Motorcontroller

Der Motortreiber ist laut Vorgabe 3,3-V-kompatibel.

```text
ESP GPIO -> 100..330R -> RPWM/LPWM/R_EN/L_EN
je Signal: 10k Pulldown nach GND
```

## Encoder

Encoder mit 3,3 V betreiben, falls möglich.

```text
3V3 -> Encoder VCC
GND -> Encoder GND
A -> 1k -> ESP GPIO, Pullup 4k7 auf 3V3
B -> 1k -> ESP GPIO, Pullup 4k7 auf 3V3
```

## Endschalter

NC-Endschalter empfohlen.

```text
ESP GPIO -> 1k -> NC Endschalter -> GND
ESP GPIO -> 4k7 -> 3V3
```

LOW = OK, HIGH = Endlage oder Kabelbruch.

## 4-Pin-Lüfter

```text
Pin 1 GND  -> GND
Pin 2 +12V -> 12V über Sicherung
Pin 3 TACH -> 1k -> ESP GPIO, 10k Pullup auf 3V3
Pin 4 PWM  -> BC547/BC337 Open Collector
```

BC547/BC337:
```text
ESP GPIO -> 4k7 -> Basis
Basis -> 100k -> GND
Emitter -> GND
Collector -> Fan PWM
```

## INA226

INA226 #1 für 12V Rail, Adresse 0x40.
INA226 #2 für 5V Rail, Adresse 0x41.

Shunt-Wert in `config/hardware.yaml` an das konkrete Modul anpassen.
