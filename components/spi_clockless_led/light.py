from esphome import pins
import esphome.codegen as cg
from esphome.components import esp32, light
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_INVERTED,
    CONF_IS_RGBW,
    CONF_MAX_REFRESH_RATE,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN,
    CONF_RGB_ORDER,
)

CODEOWNERS = ["@livingroom"]
DEPENDENCIES = ["esp32"]

CONF_SPI_HOST = "spi_host"
CONF_CLOCK_SPEED = "clock_speed"

spi_clockless_led_ns = cg.esphome_ns.namespace("spi_clockless_led")
SPIClocklessLedStrip = spi_clockless_led_ns.class_(
    "SPIClocklessLedStrip", light.AddressableLight
)

RGBOrder = spi_clockless_led_ns.enum("RGBOrder")

RGB_ORDERS = {
    "RGB": RGBOrder.ORDER_RGB,
    "RBG": RGBOrder.ORDER_RBG,
    "GRB": RGBOrder.ORDER_GRB,
    "GBR": RGBOrder.ORDER_GBR,
    "BGR": RGBOrder.ORDER_BGR,
    "BRG": RGBOrder.ORDER_BRG,
}

SPIHost = spi_clockless_led_ns.enum("SPIHost")
SPI_HOSTS = {
    "SPI2": SPIHost.SPI_HOST_2,
    "SPI3": SPIHost.SPI_HOST_3,
}

CONFIG_SCHEMA = cv.All(
    esp32.only_on_variant(supported=[esp32.VARIANT_ESP32S3, esp32.VARIANT_ESP32P4]),
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SPIClocklessLedStrip),
            cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Optional(CONF_RGB_ORDER, default="GRB"): cv.enum(RGB_ORDERS, upper=True),
            cv.Optional(CONF_IS_RGBW, default=False): cv.boolean,
            cv.Optional(CONF_SPI_HOST, default="SPI3"): cv.enum(SPI_HOSTS, upper=True),
            cv.Optional(CONF_CLOCK_SPEED, default="3200000"): cv.int_range(
                min=2000000, max=6400000
            ),
            cv.Optional(CONF_MAX_REFRESH_RATE): cv.positive_time_period_microseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    # ESP-IDF SPI-Master-Treiber sicherstellen (bei Bedarf einbinden).
    include_builtin_idf_component("esp_driver_spi")

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)

    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))
    cg.add(var.set_pin(config[CONF_PIN][CONF_NUMBER]))
    if config[CONF_PIN][CONF_INVERTED]:
        cg.add(var.set_inverted(True))
    cg.add(var.set_rgb_order(config[CONF_RGB_ORDER]))
    cg.add(var.set_is_rgbw(config[CONF_IS_RGBW]))
    cg.add(var.set_spi_host(config[CONF_SPI_HOST]))
    cg.add(var.set_clock_speed(config[CONF_CLOCK_SPEED]))
    if CONF_MAX_REFRESH_RATE in config:
        cg.add(var.set_max_refresh_rate(config[CONF_MAX_REFRESH_RATE]))
