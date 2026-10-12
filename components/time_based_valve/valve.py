# SPDX-License-Identifier: MIT
# Adapted from ESPHome 2026.8.2 time_based/cover by Sauron, 2026-10-12.
# Copyright (c) 2019 ESPHome. See LICENSE and NOTICE.
from esphome import automation
import esphome.codegen as cg
from esphome.components import valve
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_POSITION,
    CONF_ASSUMED_STATE,
    CONF_CLOSE_ACTION,
    CONF_CLOSE_DURATION,
    CONF_OPEN_ACTION,
    CONF_OPEN_DURATION,
    CONF_STOP_ACTION,
)

from . import time_based_valve_ns

TimeBasedValve = time_based_valve_ns.class_("TimeBasedValve", valve.Valve, cg.Component)
CONF_HAS_BUILT_IN_ENDSTOP = "has_built_in_endstop"
CONF_MANUAL_CONTROL = "manual_control"
CONF_START_POSITION = "start_position"
CONF_END_POSITIONS_OVERRUN = "end_positions_overrun"
CONF_OPEN_PULSE_DURATION = "open_pulse_duration"
CONF_OPEN_PAUSE_DURATION = "open_pause_duration"
CONF_CLOSE_PULSE_DURATION = "close_pulse_duration"
CONF_CLOSE_PAUSE_DURATION = "close_pause_duration"
CONF_MOVEMENT = "movement"
Movement = time_based_valve_ns.enum("Movement", is_class=True)
MOVEMENTS = {"continuous": Movement.CONTINUOUS, "pulsed": Movement.PULSED}
ControlAction = time_based_valve_ns.class_("ControlAction", automation.Action)
StartPosition = time_based_valve_ns.enum("StartPosition", is_class=True)
START_POSITIONS = {
    "asis": StartPosition.ASIS,
    "closed": StartPosition.CLOSED,
    "open": StartPosition.OPEN,
}


def travel_duration(value):
    duration = cv.positive_time_period_milliseconds(value)
    if not 1 <= duration.total_milliseconds <= 2147483647:
        raise cv.Invalid("Travel duration must be between 1ms and 2147483647ms")
    return duration


CONFIG_SCHEMA = (
    valve.valve_schema(TimeBasedValve)
    .extend(
        {
            cv.Required(CONF_OPEN_ACTION): automation.validate_automation(single=True),
            cv.Required(CONF_OPEN_DURATION): travel_duration,
            cv.Required(CONF_CLOSE_ACTION): automation.validate_automation(single=True),
            cv.Required(CONF_CLOSE_DURATION): travel_duration,
            cv.Required(CONF_STOP_ACTION): automation.validate_automation(single=True),
            cv.Optional(CONF_HAS_BUILT_IN_ENDSTOP, default=False): cv.boolean,
            cv.Optional(CONF_MANUAL_CONTROL, default=False): cv.boolean,
            cv.Optional(CONF_ASSUMED_STATE, default=True): cv.boolean,
            cv.Optional(CONF_START_POSITION, default="asis"): cv.enum(START_POSITIONS, lower=True),
            cv.Optional(CONF_END_POSITIONS_OVERRUN, default="0%"): cv.percentage,
            cv.Optional(CONF_OPEN_PULSE_DURATION, default="2s"): travel_duration,
            cv.Optional(CONF_OPEN_PAUSE_DURATION, default="8s"): travel_duration,
            cv.Optional(CONF_CLOSE_PULSE_DURATION, default="2s"): travel_duration,
            cv.Optional(CONF_CLOSE_PAUSE_DURATION, default="4s"): travel_duration,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await valve.new_valve(config)
    await cg.register_component(var, config)
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_has_built_in_endstop(config[CONF_HAS_BUILT_IN_ENDSTOP]))
    cg.add(var.set_manual_control(config[CONF_MANUAL_CONTROL]))
    cg.add(var.set_assumed_state(config[CONF_ASSUMED_STATE]))
    cg.add(var.set_start_position(config[CONF_START_POSITION]))
    cg.add(var.set_end_positions_overrun(config[CONF_END_POSITIONS_OVERRUN]))
    cg.add(var.set_open_pulse_duration(config[CONF_OPEN_PULSE_DURATION]))
    cg.add(var.set_open_pause_duration(config[CONF_OPEN_PAUSE_DURATION]))
    cg.add(var.set_close_pulse_duration(config[CONF_CLOSE_PULSE_DURATION]))
    cg.add(var.set_close_pause_duration(config[CONF_CLOSE_PAUSE_DURATION]))
    for getter, key in (
        (var.get_open_trigger, CONF_OPEN_ACTION),
        (var.get_close_trigger, CONF_CLOSE_ACTION),
        (var.get_stop_trigger, CONF_STOP_ACTION),
    ):
        await automation.build_automation(getter(), [], config[key])


@automation.register_action(
    "time_based_valve.control",
    ControlAction,
    cv.Schema({
        cv.Required(CONF_ID): cv.use_id(TimeBasedValve),
        cv.Required(CONF_POSITION): cv.templatable(cv.percentage),
        cv.Optional(CONF_MOVEMENT, default="continuous"): cv.templatable(cv.enum(MOVEMENTS, lower=True)),
    }),
    synchronous=True,
)
async def control_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    position = await cg.templatable(config[CONF_POSITION], args, cg.float_)
    movement = await cg.templatable(config[CONF_MOVEMENT], args, Movement)
    cg.add(var.set_position(position))
    cg.add(var.set_movement(movement))
    return var
