import esphome.codegen as cg
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.const import CONF_INTENSITY, CONF_NAME, CONF_OUTPUT_ID, CONF_UPDATE_INTERVAL

CODEOWNERS = ["@livingroom"]
DEPENDENCIES = ["light"]

fireplace_effect_ns = cg.esphome_ns.namespace("fireplace_effect")
RawPixelOutput = fireplace_effect_ns.class_("RawPixelOutput")
FireplaceEffect = fireplace_effect_ns.class_("FireplaceEffect", AddressableLightEffect)
FireplacePaletteEffect = fireplace_effect_ns.class_(
    "FireplacePaletteEffect", AddressableLightEffect
)
FireplaceRole = fireplace_effect_ns.enum("FireplaceRole")

ROLES = {
    "DIRECT": FireplaceRole.FIREPLACE_ROLE_DIRECT,
    "REFLECTION": FireplaceRole.FIREPLACE_ROLE_REFLECTION,
}

CONF_RAW_OUTPUT_ID = "raw_output_id"
CONF_ROLE = "role"
CONF_OUTPUT_GAIN = "output_gain"
CONF_GREEN_GAIN = "green_gain"
CONF_WHITE_GAIN = "white_gain"

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass


COMMON_EFFECT_SCHEMA = {
    cv.GenerateID(CONF_RAW_OUTPUT_ID): cv.use_id(RawPixelOutput),
    cv.Optional(CONF_ROLE, default="DIRECT"): cv.enum(ROLES, upper=True),
    cv.Optional(CONF_OUTPUT_GAIN, default="100%"): cv.percentage,
    cv.Optional(CONF_GREEN_GAIN, default="100%"): cv.percentage,
    cv.Optional(CONF_WHITE_GAIN, default="100%"): cv.percentage,
}


def _configure_common(effect, config, output):
    cg.add(effect.set_raw_output(output))
    cg.add(effect.set_role(config[CONF_ROLE]))
    cg.add(effect.set_output_gain(config[CONF_OUTPUT_GAIN]))
    cg.add(effect.set_green_gain(config[CONF_GREEN_GAIN]))
    cg.add(effect.set_white_gain(config[CONF_WHITE_GAIN]))


@register_addressable_effect(
    "fireplace",
    FireplaceEffect,
    "Fireplace",
    {
        **COMMON_EFFECT_SCHEMA,
        cv.Optional(CONF_UPDATE_INTERVAL, default="20ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INTENSITY, default="100%"): cv.percentage,
    },
)
async def fireplace_effect_to_code(config, effect_id):
    output = await cg.get_variable(config[CONF_RAW_OUTPUT_ID])
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    _configure_common(effect, config, output)
    cg.add(effect.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(effect.set_intensity(config[CONF_INTENSITY]))
    return effect


@register_addressable_effect(
    "fireplace_palette",
    FireplacePaletteEffect,
    "Fireplace Palette",
    COMMON_EFFECT_SCHEMA,
)
async def fireplace_palette_effect_to_code(config, effect_id):
    output = await cg.get_variable(config[CONF_RAW_OUTPUT_ID])
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    _configure_common(effect, config, output)
    return effect
