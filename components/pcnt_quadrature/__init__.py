"""Quadratur-Encoder ueber den PCNT-Hardwarezaehler des ESP32.

Ersetzt ESPHomes rotary_encoder, der auf diesem Board (W5500-Ethernet plus
zwei LED-Strips ueber SPI-DMA und RMT) praktisch alle Flanken verlor, weil er
per GPIO-Interrupt in Software dekodiert. Der PCNT zaehlt in Hardware weiter,
auch wenn der Mainloop blockiert.

Die Konfiguration liegt in der Sensor-Plattform (sensor.py).
"""

CODEOWNERS = ["@livingroom"]
DEPENDENCIES = ["esp32"]
