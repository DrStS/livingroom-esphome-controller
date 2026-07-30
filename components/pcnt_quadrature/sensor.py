from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INTERNAL_FILTER,
    CONF_INVERTED,
    CONF_NUMBER,
    CONF_PIN_A,
    CONF_PIN_B,
    CONF_RESOLUTION,
    CONF_VALUE,
    ICON_ROTATE_RIGHT,
    STATE_CLASS_MEASUREMENT,
    UNIT_STEPS,
)
from esphome.core import CORE

CODEOWNERS = []
DEPENDENCIES = ["esp32"]

CONF_SPEED = "speed"
CONF_INVERT_DIRECTION = "invert_direction"

pcnt_quadrature_ns = cg.esphome_ns.namespace("pcnt_quadrature")

PcntQuadratureResolution = pcnt_quadrature_ns.enum("PcntQuadratureResolution")
RESOLUTIONS = {
    1: PcntQuadratureResolution.PCNT_QUADRATURE_X1,
    2: PcntQuadratureResolution.PCNT_QUADRATURE_X2,
    4: PcntQuadratureResolution.PCNT_QUADRATURE_X4,
}

PcntQuadratureSensor = pcnt_quadrature_ns.class_(
    "PcntQuadratureSensor", sensor.Sensor, cg.PollingComponent
)
SetPositionAction = pcnt_quadrature_ns.class_(
    "SetPositionAction", automation.Action
)
ZeroPositionAction = pcnt_quadrature_ns.class_(
    "ZeroPositionAction", automation.Action
)


def validate_config(config):
    if not CORE.is_esp32:
        raise cv.Invalid("pcnt_quadrature is only supported on ESP32 targets")

    pin_a = config[CONF_PIN_A]
    pin_b = config[CONF_PIN_B]

    if pin_a[CONF_NUMBER] == pin_b[CONF_NUMBER]:
        raise cv.Invalid("pin_a and pin_b must be different GPIOs")

    # PCNT sees the physical GPIO level, not ESPHome's software inversion flag.
    # Direction inversion is therefore exposed explicitly as invert_direction.
    if pin_a.get(CONF_INVERTED, False) or pin_b.get(CONF_INVERTED, False):
        raise cv.Invalid(
            "Do not set inverted: true on pin_a/pin_b; use invert_direction instead"
        )

    if config[CONF_INTERNAL_FILTER].total_microseconds > 13:
        raise cv.Invalid(
            "Maximum PCNT internal_filter on ESP32 is 13us",
            [CONF_INTERNAL_FILTER],
        )

    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        PcntQuadratureSensor,
        unit_of_measurement=UNIT_STEPS,
        icon=ICON_ROTATE_RIGHT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.Required(CONF_PIN_A): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_PIN_B): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_RESOLUTION, default=4): cv.enum(RESOLUTIONS, int=True),
            cv.Optional(CONF_INVERT_DIRECTION, default=False): cv.boolean,
            cv.Optional(
                CONF_INTERNAL_FILTER, default="10us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(CONF_SPEED): sensor.sensor_schema(
                unit_of_measurement="steps/s",
                icon=ICON_ROTATE_RIGHT,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("1s")),
    validate_config,
)


async def to_code(config):
    # ESPHome 2026.6.x excludes this IDF component unless a consumer requests it.
    include_builtin_idf_component("esp_driver_pcnt")

    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    pin_a = await cg.gpio_pin_expression(config[CONF_PIN_A])
    cg.add(var.set_pin_a(pin_a))
    pin_b = await cg.gpio_pin_expression(config[CONF_PIN_B])
    cg.add(var.set_pin_b(pin_b))

    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_invert_direction(config[CONF_INVERT_DIRECTION]))
    cg.add(var.set_filter_us(config[CONF_INTERNAL_FILTER]))

    if CONF_SPEED in config:
        speed_sensor = await sensor.new_sensor(config[CONF_SPEED])
        cg.add(var.set_speed_sensor(speed_sensor))


@automation.register_action(
    "pcnt_quadrature.set_position",
    SetPositionAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(PcntQuadratureSensor),
            cv.Required(CONF_VALUE): cv.templatable(cv.int_),
        }
    ),
    synchronous=True,
)
async def set_position_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    value = await cg.templatable(config[CONF_VALUE], args, cg.int64)
    cg.add(var.set_value(value))
    return var


@automation.register_action(
    "pcnt_quadrature.zero",
    ZeroPositionAction,
    cv.Schema({cv.Required(CONF_ID): cv.use_id(PcntQuadratureSensor)}),
    synchronous=True,
)
async def zero_position_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)
