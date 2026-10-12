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
        "platform": "time_based_valve", "name": "Test valve",
        "open_duration": "120s", "close_duration": "120s",
        "open_action": [{"logger.log": "open"}],
        "close_action": [{"logger.log": "close"}],
        "stop_action": [{"logger.log": "stop"}],
    }],
}


class SchemaTests(unittest.TestCase):
    def validate(self, updates=None, remove=None, expected_error=None):
        config = copy.deepcopy(BASE)
        config["valve"][0].update(updates or {})
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
