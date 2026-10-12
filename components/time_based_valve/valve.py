# SPDX-License-Identifier: MIT
# Adapted from ESPHome 2026.8.2 time_based/cover by Sauron, 2026-10-12.
# Copyright (c) 2019 ESPHome. See LICENSE and NOTICE.
from esphome import automation
import esphome.codegen as cg
from esphome.components import valve
import esphome.config_validation as cv
from esphome.const import (
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
    for getter, key in (
        (var.get_open_trigger, CONF_OPEN_ACTION),
        (var.get_close_trigger, CONF_CLOSE_ACTION),
        (var.get_stop_trigger, CONF_STOP_ACTION),
    ):
        await automation.build_automation(getter(), [], config[key])
