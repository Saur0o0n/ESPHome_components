"""Render the shipped HA heartbeat template against representative source states.

This is a Jinja rendering check, not a live Home Assistant integration test.
"""
from pathlib import Path
from jinja2 import Environment, StrictUndefined
import yaml

path = Path(__file__).resolve().parents[2] / "examples/mixing_valve_climate/home-assistant.yaml"
config = yaml.safe_load(path.read_text())
template = Environment(undefined=StrictUndefined).from_string(config["actions"][0]["data"]["state"])
count = 0
for connected in ("on", "off", "unknown", "unavailable"):
    for heating in ("on", "off", "unknown", "unavailable"):
        states = {"binary_sensor.boiler_connection": connected,
                  "binary_sensor.boiler_floor_heating_active": heating}
        result = template.render(is_state=lambda key, state: states[key] == state)
        expected = "unknown" if connected != "on" or heating not in ("on", "off") else (
            "available" if heating == "on" else "unavailable")
        assert result == expected, (connected, heating, result, expected)
        count += 1
print(f"PASS: {count} HA heartbeat template input combinations")
