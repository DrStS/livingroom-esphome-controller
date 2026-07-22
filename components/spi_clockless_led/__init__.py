"""SPI-DMA clockless addressable LED strip (WS2812/SK6812) for ESP32.

Treibt einen einadrigen (clockless) LED-Streifen ueber das SPI-Peripheral mit
DMA. SPI-DMA ist -- genau wie RMT-DMA -- immun gegen Interrupt-Latenzen (z. B.
durch den W5500-Ethernet-Controller) und flackert daher nicht, auch wenn der
einzige DMA-faehige RMT-Kanal bereits von einem anderen Strip belegt ist.
"""

CODEOWNERS = ["@livingroom"]
