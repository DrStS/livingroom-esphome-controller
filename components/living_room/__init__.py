import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import output
from esphome.const import CONF_ID

CODEOWNERS = ["@mail"]

living_room_ns = cg.esphome_ns.namespace("living_room")
LivingRoom = living_room_ns.class_("LivingRoom", cg.Component)

CONF_MOTOR = "motor"
CONF_RPWM_OUTPUT = "rpwm_output"
CONF_LPWM_OUTPUT = "lpwm_output"
CONF_R_EN_PIN = "r_en_pin"
CONF_L_EN_PIN = "l_en_pin"
CONF_ENCODER_A_PIN = "encoder_a_pin"
CONF_ENCODER_B_PIN = "encoder_b_pin"
CONF_ENDSTOP_TOP_PIN = "endstop_top_pin"
CONF_ENDSTOP_BOTTOM_PIN = "endstop_bottom_pin"
CONF_TOP_POSITION_PULSES = "top_position_pulses"
CONF_SLOW_ZONE_PULSES = "slow_zone_pulses"
CONF_RAMP_MS = "ramp_ms"
CONF_CRUISE_DUTY_UP = "cruise_duty_up"
CONF_CRUISE_DUTY_DOWN = "cruise_duty_down"
CONF_HOMING_DUTY = "homing_duty"

MOTOR_SCHEMA = cv.Schema({
    cv.Required(CONF_RPWM_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Required(CONF_LPWM_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Required(CONF_R_EN_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_L_EN_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_ENCODER_A_PIN): pins.gpio_input_pin_schema,
    cv.Required(CONF_ENCODER_B_PIN): pins.gpio_input_pin_schema,
    cv.Required(CONF_ENDSTOP_TOP_PIN): pins.gpio_input_pin_schema,
    cv.Required(CONF_ENDSTOP_BOTTOM_PIN): pins.gpio_input_pin_schema,
    cv.Optional(CONF_TOP_POSITION_PULSES, default=940): cv.int_range(min=1, max=1000000),
    cv.Optional(CONF_SLOW_ZONE_PULSES, default=80): cv.int_range(min=0, max=100000),
    cv.Optional(CONF_RAMP_MS, default=800): cv.int_range(min=0, max=10000),
    cv.Optional(CONF_CRUISE_DUTY_UP, default=0.72): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_CRUISE_DUTY_DOWN, default=0.62): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_HOMING_DUTY, default=0.30): cv.float_range(min=0.0, max=1.0),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LivingRoom),
    cv.Required(CONF_MOTOR): MOTOR_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    motor = config[CONF_MOTOR]
    rpwm = await cg.get_variable(motor[CONF_RPWM_OUTPUT])
    lpwm = await cg.get_variable(motor[CONF_LPWM_OUTPUT])
    cg.add(var.set_rpwm_output(rpwm))
    cg.add(var.set_lpwm_output(lpwm))
    for key, setter in [
        (CONF_R_EN_PIN, "set_r_en_pin"),
        (CONF_L_EN_PIN, "set_l_en_pin"),
        (CONF_ENCODER_A_PIN, "set_encoder_a_pin"),
        (CONF_ENCODER_B_PIN, "set_encoder_b_pin"),
        (CONF_ENDSTOP_TOP_PIN, "set_endstop_top_pin"),
        (CONF_ENDSTOP_BOTTOM_PIN, "set_endstop_bottom_pin"),
    ]:
        pin = await cg.gpio_pin_expression(motor[key])
        cg.add(getattr(var, setter)(pin))
    cg.add(var.set_top_position_pulses(motor[CONF_TOP_POSITION_PULSES]))
    cg.add(var.set_slow_zone_pulses(motor[CONF_SLOW_ZONE_PULSES]))
    cg.add(var.set_ramp_ms(motor[CONF_RAMP_MS]))
    cg.add(var.set_cruise_duty_up(motor[CONF_CRUISE_DUTY_UP]))
    cg.add(var.set_cruise_duty_down(motor[CONF_CRUISE_DUTY_DOWN]))
    cg.add(var.set_homing_duty(motor[CONF_HOMING_DUTY]))
