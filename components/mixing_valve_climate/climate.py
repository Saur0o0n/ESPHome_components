# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sauron
import math
from esphome import automation
import esphome.codegen as cg
from esphome.components import climate, sensor, text_sensor, number
from esphome.components.time_based_valve.valve import TimeBasedValve, travel_duration
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_SENSOR, CONF_STATE, UNIT_CELSIUS, ENTITY_CATEGORY_CONFIG
from . import mixing_valve_climate_ns as ns

DEPENDENCIES = ["valve"]
AUTO_LOAD = ["sensor", "text_sensor", "number"]
MixingValveClimate = ns.class_("MixingValveClimate", climate.Climate, cg.Component)
ParameterNumber = ns.class_("ParameterNumber", number.Number)
Parameter = ns.enum("Parameter", is_class=True)
HeatSourceAction = ns.class_("HeatSourceAction", automation.Action)


def temperature(value):
    value = cv.temperature(value)
    if not math.isfinite(value):
        raise cv.Invalid("Temperature must be finite")
    return value


def positive_temperature(value):
    value = cv.temperature_delta(value)
    if not math.isfinite(value) or value <= 0:
        raise cv.Invalid("Temperature difference must be positive")
    return value


def deadband_max(config):
    # Leave at least one UI step below the independent protection threshold.
    return (math.ceil(round((config["max_temperature"] - config["max_target_temperature"]) * 10, 6)) - 1) / 10


def validate_limits(config):
    low, high = config["min_target_temperature"], config["max_target_temperature"]
    if low >= high:
        raise cv.Invalid("min_target_temperature must be below max_target_temperature")
    if not low <= config["default_target_temperature"] <= high:
        raise cv.Invalid("default_target_temperature must be inside the target limits")
    if high + config["deadband"] >= config["max_temperature"]:
        raise cv.Invalid("max_target_temperature + deadband must be below max_temperature")
    if config["overtemperature_hysteresis"] >= config["max_temperature"] - high:
        raise cv.Invalid("overtemperature_hysteresis must leave room above max_target_temperature")
    if not config["fast_close_hysteresis"] <= config["fast_close_offset"] <= 20:
        raise cv.Invalid("fast_close_offset must be between fast_close_hysteresis and 20 C")
    if "deadband_number" in config and not 0.1 <= config["deadband"] <= deadband_max(config):
        raise cv.Invalid("deadband must fit the deadband_number range below max_temperature")
    return config


CONFIG_SCHEMA = cv.All(
    climate.climate_schema(MixingValveClimate).extend({
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required("valve_id"): cv.use_id(TimeBasedValve),
        cv.Optional("default_target_temperature", default=30): temperature,
        cv.Optional("min_target_temperature", default=18): temperature,
        cv.Optional("max_target_temperature", default=46): temperature,
        cv.Optional("deadband", default=2): positive_temperature,
        cv.Optional("fast_close_offset", default=5): positive_temperature,
        cv.Optional("fast_close_hysteresis", default=1): positive_temperature,
        cv.Optional("deadband_number"): number.number_schema(
            ParameterNumber, unit_of_measurement=UNIT_CELSIUS, entity_category=ENTITY_CATEGORY_CONFIG),
        cv.Optional("fast_close_offset_number"): number.number_schema(
            ParameterNumber, unit_of_measurement=UNIT_CELSIUS, entity_category=ENTITY_CATEGORY_CONFIG),
        cv.Optional("max_temperature", default=50): temperature,
        cv.Optional("overtemperature_hysteresis", default=2): positive_temperature,
        cv.Optional("sensor_timeout", default="30s"): travel_duration,
        cv.Optional("heat_source_required", default=False): cv.boolean,
        cv.Optional("heat_source_timeout", default="90s"): travel_duration,
        cv.Optional("default_mode", default="OFF"): cv.enum({
            "OFF": climate.CLIMATE_MODES["OFF"], "AUTO": climate.CLIMATE_MODES["AUTO"]}, upper=True),
        cv.Optional("restore_state", default=True): cv.boolean,
        cv.Optional("status"): text_sensor.text_sensor_schema(),
    }).extend(cv.COMPONENT_SCHEMA), validate_limits,
)


def final_validate(config):
    full = fv.full_config.get()
    path = full.get_path_for_id(config["valve_id"])[:-1]
    valve_config = full.get_config_for_path(path)
    if valve_config.get("manual_control", False):
        raise cv.Invalid("mixing_valve_climate requires manual_control: false on its valve")
    owners = full.data.setdefault("mixing_valve_climate_owners", {})
    valve_id = str(config["valve_id"])
    owner = str(config[CONF_ID])
    if valve_id in owners and owners[valve_id] != owner:
        raise cv.Invalid("Only one mixing_valve_climate may control each valve_id")
    owners[valve_id] = owner
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    cg.add(var.set_sensor(await cg.get_variable(config[CONF_SENSOR])))
    cg.add(var.set_valve(await cg.get_variable(config["valve_id"])))
    for key in ("default_target_temperature", "min_target_temperature", "max_target_temperature",
                "deadband", "fast_close_offset", "fast_close_hysteresis", "max_temperature", "overtemperature_hysteresis", "sensor_timeout",
                "heat_source_required", "heat_source_timeout", "default_mode", "restore_state"):
        cg.add(getattr(var, "set_" + key)(config[key]))
    for key, parameter, low, high in (
        ("deadband_number", Parameter.DEADBAND, 0.1, deadband_max(config)),
        ("fast_close_offset_number", Parameter.FAST_CLOSE_OFFSET, config["fast_close_hysteresis"], 20),
    ):
        if key in config:
            entity = await number.new_number(config[key], var, parameter, min_value=low, max_value=high, step=0.1)
            cg.add(getattr(var, "set_" + key)(entity))
    if "status" in config:
        cg.add(var.set_status_sensor(await text_sensor.new_text_sensor(config["status"])))


@automation.register_action(
    "mixing_valve_climate.set_heat_source", HeatSourceAction,
    cv.Schema({cv.Required(CONF_ID): cv.use_id(MixingValveClimate),
               cv.Required(CONF_STATE): cv.templatable(cv.one_of("available", "unavailable", "unknown", lower=True))}),
    synchronous=True,
)
async def heat_source_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))
    cg.add(var.set_state(await cg.templatable(config[CONF_STATE], args, cg.std_string)))
    return var
