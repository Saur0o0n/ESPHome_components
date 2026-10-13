"""Validate the external component via the actual ESPHome configuration loader."""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
BASE = {
    "esphome": {"name": "time-based-valve-schema"},
    "host": {},
    "logger": {},
    "external_components": [{
        "source": {"type": "local", "path": str(ROOT / "components")},
        "components": ["time_based_valve"],
    }],
    "valve": [{
        "platform": "time_based_valve", "name": "Test valve", "id": "test_valve",
        "open_duration": "120s", "close_duration": "120s",
        "open_action": [{"logger.log": "open"}],
        "close_action": [{"logger.log": "close"}],
        "stop_action": [{"logger.log": "stop"}],
    }],
}


class SchemaTests(unittest.TestCase):
    def validate(self, updates=None, remove=None, expected_error=None, action=None):
        config = copy.deepcopy(BASE)
        config["valve"][0].update(updates or {})
        if action is not None:
            config["esphome"]["on_boot"] = {"then": [{"time_based_valve.control": action}]}
        if remove:
            del config["valve"][0][remove]
        with tempfile.TemporaryDirectory(prefix="valve-schema-") as tmp:
            path = Path(tmp) / "test.yaml"
            path.write_text(yaml.safe_dump(config))
            result = subprocess.run(
                [sys.executable, "-m", "esphome", "config", str(path)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
        if expected_error:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(expected_error, result.stdout)
        else:
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_direction_change_wait(self):
        for value in ("0ms", "200ms", "2147483647ms"):
            self.validate({"direction_change_wait_time": value})
        for value in ("-1ms", "2147483648ms"):
            self.validate({"direction_change_wait_time": value}, expected_error="direction_change_wait_time")

    def test_pulse_timing(self):
        keys = ("open_pulse_duration", "open_pause_duration", "close_pulse_duration", "close_pause_duration")
        self.validate(dict(zip(keys, ["1s", "7s", "500ms", "3s"])))
        for key in keys:
            self.validate({key: "2147483647ms"})
            for value in ("0ms", "-1ms", "2147483648ms"):
                with self.subTest(key=key, value=value):
                    self.validate({key: value}, expected_error=key)

    def test_movement_action(self):
        for mode in ("continuous", "pulsed"):
            for pos in ("0%", "30%", "100%"):
                self.validate(action={"id": "test_valve", "position": pos, "movement": mode})
        self.validate(action={"id": "test_valve", "position": "30%"})

    def test_invalid_movement_action(self):
        self.validate(action={"id": "test_valve", "position": "30%", "movement": "slow"},
                      expected_error="Unknown value")
        for pos in ("-1%", "101%"):
            self.validate(action={"id": "test_valve", "position": pos}, expected_error="position")
        self.validate(action={"id": "test_valve"}, expected_error="position")
        self.validate(action={"position": "30%"}, expected_error="id")
        self.validate(action={"id": "missing_valve", "position": "30%"}, expected_error="missing_valve")

    def test_defaults(self):
        self.validate()

    def test_start_positions(self):
        for position in ("asis", "closed", "open"):
            with self.subTest(position=position):
                self.validate({"start_position": position, "end_positions_overrun": "5%"})

    def test_invalid_start_position(self):
        self.validate({"start_position": "middle"}, expected_error="Unknown value")

    def test_overrun_bounds(self):
        self.validate({"end_positions_overrun": "100%"})
        for value in ("-1%", "101%", "5s"):
            with self.subTest(value=value):
                self.validate({"end_positions_overrun": value}, expected_error="end_positions_overrun")

    def test_options_and_asymmetric_durations(self):
        self.validate({"manual_control": True, "has_built_in_endstop": True,
                       "assumed_state": False, "close_duration": "90s"})

    def test_zero_duration(self):
        for key in ("open_duration", "close_duration"):
            with self.subTest(key=key):
                self.validate({key: "0ms"}, expected_error="Travel duration must be")

    def test_overflow_duration(self):
        self.validate({"open_duration": "2147483648ms"}, expected_error="Travel duration must be")

    def test_negative_duration(self):
        self.validate({"close_duration": "-1s"}, expected_error="close_duration")

    def test_missing_required_keys(self):
        for key in ("open_duration", "close_duration", "open_action", "close_action", "stop_action"):
            with self.subTest(key=key):
                self.validate(remove=key, expected_error=f"'{key}' is a required option")

    def test_unknown_option(self):
        self.validate({"has_position": True}, expected_error="invalid option")


if __name__ == "__main__":
    unittest.main(verbosity=2)
