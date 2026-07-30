"""Produktionsreife Positionsregelung des TV-Lifts auf einem FreeRTOS-Task."""

import esphome.codegen as cg
from esphome.components import output, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@livingroom"]
DEPENDENCIES = ["esp32"]

CONF_RPWM = "rpwm"
CONF_LPWM = "lpwm"
CONF_ENABLE = "enable"
CONF_ENCODER = "encoder"
CONF_MIN_POSITION = "min_position"
CONF_MAX_POSITION = "max_position"
CONF_PERIOD = "period"
CONF_TASK_CORE = "task_core"
CONF_TASK_PRIORITY = "task_priority"
CONF_TOLERANCE = "tolerance"
CONF_DUTY_MIN_UP = "duty_min_up"
CONF_DUTY_MIN_DOWN = "duty_min_down"
CONF_DUTY_MAX = "duty_max"
CONF_ERROR_FULL = "error_full"
CONF_FINE_WINDOW = "fine_window"
CONF_PULSE_PERIOD = "pulse_period"
CONF_BRAKE = "brake_time"
CONF_STALL = "stall_time"
CONF_TIMEOUT = "timeout"
CONF_MANUAL_TIMEOUT = "manual_timeout"
# Trapezprofil und PI-Geschwindigkeitsregler (Encoder-Counts pro Sekunde).
CONF_MAX_SPEED = "max_speed"
CONF_ACCEL = "accel"
CONF_DECEL = "decel"
CONF_APPROACH_SPEED = "approach_speed"
CONF_SPEED_KP = "speed_kp"
CONF_SPEED_KI = "speed_ki"
CONF_SPEED_WINDOW = "speed_window"
CONF_SPEED_FILTER = "speed_filter"
# Persistenz der Position/Referenz im NVS (Stromausfallsicherheit).
CONF_PERSIST_POSITION = "persist_position"

lift_motor_ns = cg.esphome_ns.namespace("lift_motor")
pcnt_quadrature_ns = cg.esphome_ns.namespace("pcnt_quadrature")
PcntQuadratureSensor = pcnt_quadrature_ns.class_(
    "PcntQuadratureSensor", sensor.Sensor, cg.PollingComponent
)
LiftMotor = lift_motor_ns.class_("LiftMotor", cg.Component)


def validate_limits(config):
    if config[CONF_MAX_POSITION] <= config[CONF_MIN_POSITION]:
        raise cv.Invalid(
            "max_position must be greater than min_position",
            [CONF_MAX_POSITION],
        )
    # Kriechgeschwindigkeit muss innerhalb des Profils liegen, sonst wuerde der
    # Sollwert im Anfahrbereich ueber die Maximalgeschwindigkeit springen.
    if config[CONF_APPROACH_SPEED] > config[CONF_MAX_SPEED]:
        raise cv.Invalid(
            "approach_speed must not exceed max_speed",
            [CONF_APPROACH_SPEED],
        )
    # Das Messfenster muss mehrere Regelzyklen umfassen, sonst rauscht die
    # Geschwindigkeit durch die Encoderquantisierung.
    if config[CONF_SPEED_WINDOW].total_milliseconds < 2 * config[CONF_PERIOD].total_milliseconds:
        raise cv.Invalid(
            "speed_window must be at least twice the control period",
            [CONF_SPEED_WINDOW],
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LiftMotor),
            cv.Required(CONF_RPWM): cv.use_id(output.FloatOutput),
            cv.Required(CONF_LPWM): cv.use_id(output.FloatOutput),
            cv.Required(CONF_ENABLE): cv.use_id(output.BinaryOutput),
            cv.Required(CONF_ENCODER): cv.use_id(PcntQuadratureSensor),
            cv.Required(CONF_MIN_POSITION): cv.int_,
            cv.Required(CONF_MAX_POSITION): cv.int_,
            cv.Optional(CONF_PERIOD, default="1ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TASK_CORE, default=1): cv.int_range(min=0, max=1),
            cv.Optional(CONF_TASK_PRIORITY, default=10): cv.int_range(min=1, max=20),
            cv.Optional(CONF_TOLERANCE, default=2): cv.int_range(min=0, max=10000),
            cv.Optional(CONF_DUTY_MIN_UP, default=0.14): cv.percentage,
            cv.Optional(CONF_DUTY_MIN_DOWN, default=0.12): cv.percentage,
            cv.Optional(CONF_DUTY_MAX, default=0.35): cv.percentage,
            # error_full stammt aus der alten Duty-Kennlinie und wird von der
            # Geschwindigkeitsregelung nicht mehr benutzt (nur Kompatibilitaet).
            cv.Optional(CONF_ERROR_FULL, default=12.0): cv.positive_float,
            # --- Trapezprofil in Counts/s bzw. Counts/s^2 ---
            cv.Optional(CONF_MAX_SPEED, default=400.0): cv.float_range(min=1.0, max=20000.0),
            cv.Optional(CONF_ACCEL, default=800.0): cv.float_range(min=1.0, max=200000.0),
            cv.Optional(CONF_DECEL, default=800.0): cv.float_range(min=1.0, max=200000.0),
            cv.Optional(CONF_APPROACH_SPEED, default=60.0): cv.float_range(min=0.0, max=20000.0),
            # --- PI-Regler: Duty pro Count/s bzw. Duty pro Count ---
            cv.Optional(CONF_SPEED_KP, default=0.0006): cv.float_range(min=0.0, max=1.0),
            cv.Optional(CONF_SPEED_KI, default=0.004): cv.float_range(min=0.0, max=10.0),
            # --- Geschwindigkeitsmessung ---
            cv.Optional(
                CONF_SPEED_WINDOW, default="20ms"
            ): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(milliseconds=2), max=cv.TimePeriod(milliseconds=500)),
            ),
            cv.Optional(CONF_SPEED_FILTER, default=0.35): cv.float_range(min=0.01, max=1.0),
            # --- Persistenz: Position/Referenz im NVS halten ---
            # Geschrieben wird nur bei Fahrtbeginn, Fahrtende und Referenz-
            # wechsel. Kein periodisches Speichern -- das wuerde unnoetig
            # Flash-Schreibzyklen verbrauchen.
            cv.Optional(CONF_PERSIST_POSITION, default=True): cv.boolean,
            cv.Optional(CONF_FINE_WINDOW, default=6): cv.int_range(min=0, max=10000),
            cv.Optional(CONF_PULSE_PERIOD, default=4): cv.int_range(min=1, max=100),
            cv.Optional(CONF_BRAKE, default="200ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_STALL, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIMEOUT, default="4000ms"): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_MANUAL_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_rpwm(await cg.get_variable(config[CONF_RPWM])))
    cg.add(var.set_lpwm(await cg.get_variable(config[CONF_LPWM])))
    cg.add(var.set_enable(await cg.get_variable(config[CONF_ENABLE])))
    cg.add(var.set_encoder(await cg.get_variable(config[CONF_ENCODER])))
    cg.add(var.set_min_position(config[CONF_MIN_POSITION]))
    cg.add(var.set_max_position(config[CONF_MAX_POSITION]))

    cg.add(var.set_period_ms(config[CONF_PERIOD]))
    cg.add(var.set_task_core(config[CONF_TASK_CORE]))
    cg.add(var.set_task_priority(config[CONF_TASK_PRIORITY]))
    cg.add(var.set_tolerance(config[CONF_TOLERANCE]))
    cg.add(var.set_duty_min_up(config[CONF_DUTY_MIN_UP]))
    cg.add(var.set_duty_min_down(config[CONF_DUTY_MIN_DOWN]))
    cg.add(var.set_duty_max(config[CONF_DUTY_MAX]))
    cg.add(var.set_error_full(config[CONF_ERROR_FULL]))
    cg.add(var.set_fine_window(config[CONF_FINE_WINDOW]))
    cg.add(var.set_pulse_period(config[CONF_PULSE_PERIOD]))
    cg.add(var.set_brake_ms(config[CONF_BRAKE]))
    cg.add(var.set_stall_ms(config[CONF_STALL]))
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT]))
    cg.add(var.set_manual_timeout_ms(config[CONF_MANUAL_TIMEOUT]))

    # Trapezprofil und Geschwindigkeitsregler.
    cg.add(var.set_max_speed(config[CONF_MAX_SPEED]))
    cg.add(var.set_accel(config[CONF_ACCEL]))
    cg.add(var.set_decel(config[CONF_DECEL]))
    cg.add(var.set_approach_speed(config[CONF_APPROACH_SPEED]))
    cg.add(var.set_speed_kp(config[CONF_SPEED_KP]))
    cg.add(var.set_speed_ki(config[CONF_SPEED_KI]))
    cg.add(var.set_speed_window_ms(config[CONF_SPEED_WINDOW]))
    cg.add(var.set_speed_filter(config[CONF_SPEED_FILTER]))

    # Persistenz.
    cg.add(var.set_persist_position(config[CONF_PERSIST_POSITION]))
