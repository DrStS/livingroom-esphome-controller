"""Serialisierter RMT-LED-Strip (ESP32-S3).

Duenne Ableitung von ESPHomes esp32_rmt_led_strip, die den RMT-Transfer
blockierend macht (wartet auf DMA-Ende). Verhindert, dass der RMT-DMA-Stream
gleichzeitig mit dem SPI-DMA-Transfer eines zweiten Strips laeuft (GDMA-Kollision
auf dem S3). Siehe serialized_rmt_led.h fuer Details.
"""

CODEOWNERS = ["@livingroom"]
