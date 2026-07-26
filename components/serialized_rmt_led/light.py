"""ESPHome light platform for the RMT strip with cross-DMA arbitration.

The complete esp32_rmt_led_strip schema/codegen is reused. Only the generated
C++ class is replaced by SerializedRMTLedStrip, which keeps RMT asynchronous,
releases the shared LED-DMA token from the RMT TX-done callback and additionally
implements fireplace_effect::RawPixelOutput.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32_rmt_led_strip import light as rmt_light
from esphome.components.fireplace_effect import RawPixelOutput
from esphome.const import CONF_OUTPUT_ID

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["esp32_rmt_led_strip", "fireplace_effect"]
CODEOWNERS = ["@livingroom"]

serialized_rmt_led_ns = cg.esphome_ns.namespace("serialized_rmt_led")
SerializedRMTLedStrip = serialized_rmt_led_ns.class_(
    "SerializedRMTLedStrip", rmt_light.ESP32RMTLEDStripLightOutput, RawPixelOutput
)


def _use_serialized_output(config):
    # WICHTIG: Den Typ der Output-ID bereits waehrend der Validierung auf unsere
    # Unterklasse setzen (nicht erst in to_code). Nur so sieht die spaetere
    # use_id(RawPixelOutput)-Aufloesung den korrekten, von RawPixelOutput
    # erbenden Typ. Wird der Typ erst in to_code gesetzt, schlaegt die
    # raw_output_id-Aufloesung fehl ("doesn't inherit from RawPixelOutput").
    config[CONF_OUTPUT_ID].type = SerializedRMTLedStrip
    return config


CONFIG_SCHEMA = cv.All(rmt_light.CONFIG_SCHEMA, _use_serialized_output)


async def to_code(config):
    # config[CONF_OUTPUT_ID].type ist bereits SerializedRMTLedStrip -> ESPHomes
    # RMT-Codegen (Pin, Chipset-Timings, DMA, RGB-Order ...) erzeugt via
    # cg.new_Pvariable eine Instanz unserer Unterklasse.
    await rmt_light.to_code(config)
