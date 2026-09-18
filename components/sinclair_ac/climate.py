#based on: https://github.com/DomiStyle/esphome-panasonic-ac

from esphome.const import (
    CONF_ID,
)
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, sensor, select, switch, remote_base

AUTO_LOAD = ["switch", "sensor", "select", "remote_base"]
DEPENDENCIES = ["uart"]

sinclair_ac_ns = cg.esphome_ns.namespace("sinclair_ac")
SinclairAC = sinclair_ac_ns.class_(
    "SinclairAC", cg.Component, uart.UARTDevice, climate.Climate
)
sinclair_ac_cnt_ns = sinclair_ac_ns.namespace("CNT")
SinclairACCNT = sinclair_ac_cnt_ns.class_("SinclairACCNT", SinclairAC)

SinclairACSwitch = sinclair_ac_ns.class_(
    "SinclairACSwitch", switch.Switch, cg.Component
)
SinclairACSelect = sinclair_ac_ns.class_(
    "SinclairACSelect", select.Select, cg.Component
)


CONF_HORIZONTAL_SWING_SELECT    = "horizontal_swing_select"
CONF_VERTICAL_SWING_SELECT      = "vertical_swing_select"
CONF_DISPLAY_SELECT             = "display_select"
CONF_DISPLAY_UNIT_SELECT        = "display_unit_select"

CONF_PLASMA_SWITCH              = "plasma_switch"
CONF_SLEEP_SWITCH               = "sleep_switch"
CONF_XFAN_SWITCH                = "xfan_switch"
CONF_SAVE_SWITCH                = "save_switch"
CONF_BEEPER_SWITCH              = "beeper_switch"

CONF_CURRENT_TEMPERATURE_SENSOR = "current_temperature_sensor"

CONF_FAN_SPEEDS                 = "fan_speeds"        # 5 (Sinclair MV-H09BIF) or 3 (most Gree-based units)
CONF_QUIET_MODE                 = "quiet_mode"        # False to hide the Quiet fan mode
CONF_TURBO_MODE                 = "turbo_mode"        # False to hide the Turbo fan mode
CONF_HORIZONTAL_SWING           = "horizontal_swing"  # False for units without motorized horizontal louvers
CONF_CURRENT_TEMPERATURE_FORMULA = "current_temperature_formula"  # sinclair: (raw-16)/2, gree: raw-40
CONF_DISPLAY_MODES              = "display_modes"     # subset of display modes offered in display_select
CONF_PARTIAL_SWING              = "partial_swing"     # False for units that only swing over the full range

# I FEEL over IR: the unit accepts the room temperature only from the IR remote, so the component can
# play the remote through a remote_transmitter (IR LED, or wired to the IR receiver output)
CONF_IR_TRANSMITTER_ID          = "ir_transmitter_id"
CONF_I_FEEL_SENSOR              = "i_feel_sensor"     # temperature sent to the unit as I FEEL
CONF_I_FEEL_INTERVAL            = "i_feel_interval"   # resend period of the temperature frame
CONF_I_FEEL_SWITCH              = "i_feel_switch"     # optional switch to turn I FEEL on/off from HA
CONF_I_FEEL_HEADER_MARK         = "i_feel_header_mark"   # header of the temperature frame, microseconds
CONF_I_FEEL_HEADER_SPACE        = "i_feel_header_space"

# keys for CONF_DISPLAY_MODES, same order as DISPLAY_OPTIONS; bit i of the mask passed to C++
DISPLAY_MODE_KEYS = ["display_off", "auto", "set_temperature", "actual_temperature", "outside_temperature"]
CONF_BEEPER_BYTE                = "beeper_byte"       # experimental: data byte carrying the silent flag
CONF_BEEPER_MASK                = "beeper_mask"       # experimental: bit mask of the silent flag

HORIZONTAL_SWING_OPTIONS = [
    "0 - OFF",
    "1 - Swing - Full",
    "2 - Constant - Left",
    "3 - Constant - Mid-Left",
    "4 - Constant - Middle",
    "5 - Constant - Mid-Right",
    "6 - Constant - Right",
]


VERTICAL_SWING_OPTIONS = [
    "00 - OFF",
    "01 - Swing - Full",
    "02 - Swing - Down",
    "03 - Swing - Mid-Down",
    "04 - Swing - Middle",
    "05 - Swing - Mid-Up",
    "06 - Swing - Up",
    "07 - Constant - Down",
    "08 - Constant - Mid-Down",
    "09 - Constant - Middle",
    "10 - Constant - Mid-Up",
    "11 - Constant - Up",
]

# indexes of the partial swing ranges in VERTICAL_SWING_OPTIONS (hidden with partial_swing: false)
PARTIAL_SWING_INDEXES = (2, 3, 4, 5, 6)

DISPLAY_OPTIONS = [
    "0 - OFF",
    "1 - Auto",
    "2 - Set temperature",
    "3 - Actual temperature",
    "4 - Outside temperature",
]

DISPLAY_UNIT_OPTIONS = [
    "C",
    "F",
]

switch_schema = switch.switch_schema(switch.Switch).extend(cv.COMPONENT_SCHEMA).extend(
    {cv.GenerateID(): cv.declare_id(SinclairACSwitch)}
)
# The unit does not report the beeper flag back, so the switch keeps its own state.
# Default ON = unit beeps on every command (stock behaviour).
beeper_switch_schema = switch.switch_schema(
    switch.Switch, default_restore_mode="RESTORE_DEFAULT_ON"
).extend(cv.COMPONENT_SCHEMA).extend(
    {cv.GenerateID(): cv.declare_id(SinclairACSwitch)}
)
select_schema = select.select_schema(select.Select).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(SinclairACSelect)}
)

SCHEMA = climate.climate_schema(climate.Climate).extend(
    {
        cv.Optional(CONF_HORIZONTAL_SWING_SELECT): select_schema,
        cv.Optional(CONF_VERTICAL_SWING_SELECT): select_schema,
        cv.Optional(CONF_DISPLAY_SELECT): select_schema,
        cv.Optional(CONF_DISPLAY_UNIT_SELECT): select_schema,
        cv.Optional(CONF_PLASMA_SWITCH): switch_schema,
        cv.Optional(CONF_SLEEP_SWITCH): switch_schema,
        cv.Optional(CONF_XFAN_SWITCH): switch_schema,
        cv.Optional(CONF_SAVE_SWITCH): switch_schema,
        cv.Optional(CONF_BEEPER_SWITCH): beeper_switch_schema,
    }
).extend(uart.UART_DEVICE_SCHEMA)

def _validate_capabilities(config):
    if not config[CONF_HORIZONTAL_SWING] and CONF_HORIZONTAL_SWING_SELECT in config:
        raise cv.Invalid(
            f"{CONF_HORIZONTAL_SWING_SELECT} cannot be used with {CONF_HORIZONTAL_SWING}: false"
        )
    if not config[CONF_DISPLAY_MODES]:
        raise cv.Invalid(f"{CONF_DISPLAY_MODES} must contain at least one mode")
    if CONF_I_FEEL_SENSOR in config and CONF_IR_TRANSMITTER_ID not in config:
        raise cv.Invalid(f"{CONF_I_FEEL_SENSOR} requires {CONF_IR_TRANSMITTER_ID}")
    if CONF_I_FEEL_SWITCH in config and CONF_I_FEEL_SENSOR not in config:
        raise cv.Invalid(f"{CONF_I_FEEL_SWITCH} requires {CONF_I_FEEL_SENSOR}")
    return config


CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SinclairACCNT),
            cv.Optional(CONF_CURRENT_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_FAN_SPEEDS, default=5): cv.one_of(3, 5, int=True),
            cv.Optional(CONF_QUIET_MODE, default=True): cv.boolean,
            cv.Optional(CONF_TURBO_MODE, default=True): cv.boolean,
            cv.Optional(CONF_HORIZONTAL_SWING, default=True): cv.boolean,
            cv.Optional(CONF_PARTIAL_SWING, default=True): cv.boolean,
            cv.Optional(CONF_CURRENT_TEMPERATURE_FORMULA, default="sinclair"): cv.one_of(
                "sinclair", "gree", lower=True
            ),
            # "off" would be parsed by YAML as boolean false, hence the key is display_off
            cv.Optional(CONF_DISPLAY_MODES, default=DISPLAY_MODE_KEYS): cv.ensure_list(
                cv.one_of(*DISPLAY_MODE_KEYS, lower=True)
            ),
            cv.Optional(CONF_IR_TRANSMITTER_ID): cv.use_id(remote_base.RemoteTransmitterBase),
            cv.Optional(CONF_I_FEEL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_I_FEEL_INTERVAL, default="1min"): cv.All(
                cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(seconds=10))
            ),
            cv.Optional(CONF_I_FEEL_SWITCH): beeper_switch_schema,
            cv.Optional(CONF_I_FEEL_HEADER_MARK, default=6000): cv.int_range(min=1000, max=20000),
            cv.Optional(CONF_I_FEEL_HEADER_SPACE, default=3000): cv.int_range(min=1000, max=20000),
            cv.Optional(CONF_BEEPER_BYTE, default=40): cv.int_range(min=0, max=44),
            cv.Optional(CONF_BEEPER_MASK, default=0x01): cv.int_range(min=1, max=255),
        }
    ),
    _validate_capabilities,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_fan_speeds(config[CONF_FAN_SPEEDS]))
    cg.add(var.set_quiet_mode(config[CONF_QUIET_MODE]))
    cg.add(var.set_turbo_mode(config[CONF_TURBO_MODE]))
    cg.add(var.set_horizontal_swing(config[CONF_HORIZONTAL_SWING]))
    cg.add(var.set_partial_swing(config[CONF_PARTIAL_SWING]))
    cg.add(var.set_current_temperature_gree(config[CONF_CURRENT_TEMPERATURE_FORMULA] == "gree"))
    cg.add(var.set_beeper_flag(config[CONF_BEEPER_BYTE], config[CONF_BEEPER_MASK]))
    display_mask = 0
    for i, key in enumerate(DISPLAY_MODE_KEYS):
        if key in config[CONF_DISPLAY_MODES]:
            display_mask |= 1 << i
    cg.add(var.set_display_modes(display_mask))

    if CONF_IR_TRANSMITTER_ID in config:
        ir_tx = await cg.get_variable(config[CONF_IR_TRANSMITTER_ID])
        cg.add(var.set_ir_transmitter(ir_tx))
    if CONF_I_FEEL_SENSOR in config:
        sens = await cg.get_variable(config[CONF_I_FEEL_SENSOR])
        cg.add(var.set_i_feel_sensor(sens))
        cg.add(var.set_i_feel_interval(config[CONF_I_FEEL_INTERVAL]))
        cg.add(var.set_i_feel_header(config[CONF_I_FEEL_HEADER_MARK], config[CONF_I_FEEL_HEADER_SPACE]))

    if CONF_HORIZONTAL_SWING_SELECT in config:
        conf = config[CONF_HORIZONTAL_SWING_SELECT]
        hswing_select = await select.new_select(conf, options=HORIZONTAL_SWING_OPTIONS)
        await cg.register_component(hswing_select, conf)
        cg.add(var.set_horizontal_swing_select(hswing_select))

    if CONF_VERTICAL_SWING_SELECT in config:
        conf = config[CONF_VERTICAL_SWING_SELECT]
        # without partial_swing only Off, full swing and the fixed positions are offered
        vswing_options = [
            o for i, o in enumerate(VERTICAL_SWING_OPTIONS)
            if config[CONF_PARTIAL_SWING] or i not in PARTIAL_SWING_INDEXES
        ]
        vswing_select = await select.new_select(conf, options=vswing_options)
        await cg.register_component(vswing_select, conf)
        cg.add(var.set_vertical_swing_select(vswing_select))
    
    if CONF_DISPLAY_SELECT in config:
        conf = config[CONF_DISPLAY_SELECT]
        enabled = config[CONF_DISPLAY_MODES]
        display_options = [
            label for key, label in zip(DISPLAY_MODE_KEYS, DISPLAY_OPTIONS) if key in enabled
        ]
        display_select = await select.new_select(conf, options=display_options)
        await cg.register_component(display_select, conf)
        cg.add(var.set_display_select(display_select))
    
    if CONF_DISPLAY_UNIT_SELECT in config:
        conf = config[CONF_DISPLAY_UNIT_SELECT]
        display_unit_select = await select.new_select(conf, options=DISPLAY_UNIT_OPTIONS)
        await cg.register_component(display_unit_select, conf)
        cg.add(var.set_display_unit_select(display_unit_select))


    if CONF_CURRENT_TEMPERATURE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_CURRENT_TEMPERATURE_SENSOR])
        cg.add(var.set_current_temperature_sensor(sens))
        
    for s in [CONF_PLASMA_SWITCH, CONF_SLEEP_SWITCH, CONF_XFAN_SWITCH, CONF_SAVE_SWITCH, CONF_BEEPER_SWITCH, CONF_I_FEEL_SWITCH]:
        if s in config:
            conf = config[s]
            a_switch = cg.new_Pvariable(conf[CONF_ID])
            await cg.register_component(a_switch, conf)
            await switch.register_switch(a_switch, conf)
            cg.add(getattr(var, f"set_{s}")(a_switch))
