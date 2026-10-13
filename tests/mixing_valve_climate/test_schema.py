"""Exercise complete ESPHome configuration validation, including references."""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/time_based_valve"))
from test_schema import BASE as VALVE_BASE

BASE = copy.deepcopy(VALVE_BASE)
BASE["external_components"][0]["components"].append("mixing_valve_climate")
BASE["sensor"] = [{"platform": "template", "id": "water"}]
BASE["climate"] = [{"platform": "mixing_valve_climate", "id": "controller", "name": "Water", "sensor": "water", "valve_id": "test_valve"}]

class SchemaTests(unittest.TestCase):
    def validate(self, updates=None, missing=None, action=None, error=None, duplicate=False, manual=False):
        config = copy.deepcopy(BASE)
        config["climate"][0].update(updates or {})
        config["valve"][0]["manual_control"] = manual
        if duplicate:
            other = copy.deepcopy(config["climate"][0])
            other["id"] = "second_controller"
            other["name"] = "Second controller"
            config["climate"].append(other)
        if missing:
            del config["climate"][0][missing]
        if action is not None:
            config["esphome"]["on_boot"] = {"then": [{"mixing_valve_climate.set_heat_source": action}]}
        with tempfile.TemporaryDirectory(prefix="mixing-schema-") as tmp:
            path = Path(tmp) / "test.yaml"
            path.write_text(yaml.safe_dump(config))
            result = subprocess.run([sys.executable, "-m", "esphome", "config", str(path)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if error:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(error, result.stdout)
        else:
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_defaults_and_options(self):
        self.validate()
        self.validate({"default_mode": "AUTO", "restore_state": False, "heat_source_required": True,
                       "sensor_timeout": "45s", "heat_source_timeout": "120s", "deadband": "1°C",
                       "status": {"name": "Regulation status"}})

    def test_fast_closing_and_numbers(self):
        self.validate({"fast_close_offset": 5, "fast_close_hysteresis": 1,
                       "deadband_number": {"name": "Tolerance"},
                       "fast_close_offset_number": {"name": "Fast close offset"}})
        self.validate({"deadband": 3.9, "deadband_number": {"name": "Tolerance"}})
        for updates, error in [
            ({"fast_close_offset": 0}, "positive"),
            ({"fast_close_hysteresis": 0}, "positive"),
            ({"fast_close_offset": 0.5}, "fast_close_offset"),
            ({"fast_close_offset": 21}, "fast_close_offset"),
            ({"fast_close_hysteresis": 6}, "fast_close_offset"),
            ({"deadband": 3.99, "deadband_number": {"name": "Tolerance"}}, "deadband_number"),
        ]:
            self.validate(updates, error=error)

    def test_missing_references(self):
        for key in ("sensor", "valve_id"):
            self.validate(missing=key, error=key)
            self.validate({key: "missing"}, error="missing")
        self.validate({"valve_id": "water"}, error="valve_id")

    def test_exclusive_valve_ownership(self):
        self.validate(duplicate=True, error="Only one")
        self.validate(manual=True, error="manual_control")

    def test_temperature_limits(self):
        for updates, error in [
            ({"deadband": 0}, "positive"), ({"deadband": -1}, "positive"),
            ({"default_target_temperature": float("nan")}, "default_target_temperature"),
            ({"max_temperature": float("inf")}, "max_temperature"),
            ({"min_target_temperature": 46}, "min_target_temperature"),
            ({"default_target_temperature": 60}, "default_target_temperature"),
            ({"max_temperature": 48}, "deadband"),
            ({"overtemperature_hysteresis": 4}, "overtemperature_hysteresis"),
        ]:
            with self.subTest(updates=updates):
                self.validate(updates, error=error)

    def test_timeouts(self):
        for key in ("sensor_timeout", "heat_source_timeout"):
            for value in ("0s", "-1s", "2147483648ms"):
                self.validate({key: value}, error=key)

    def test_modes_and_action(self):
        self.validate({"default_mode": "HEAT_COOL"}, error="Unknown value")
        for state in ("available", "unavailable", "unknown"):
            self.validate(action={"id": "controller", "state": state})
        self.validate(action={"id": "controller", "state": "yes"}, error="state")
        self.validate(action={"id": "test_valve", "state": "unknown"}, error="id")

if __name__ == "__main__":
    unittest.main(verbosity=2)
