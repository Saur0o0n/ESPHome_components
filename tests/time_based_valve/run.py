"""Run with Python from an environment that has ESPHome installed."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args, **kwargs):
    subprocess.run(args, cwd=ROOT, check=True, **kwargs)


run(sys.executable, "tests/time_based_valve/test_schema.py")
run(sys.executable, "-m", "esphome", "compile", "tests/time_based_valve/host.yaml")
binary = ROOT / "tests/time_based_valve/.esphome/build/time-based-valve-test/.pioenvs/time-based-valve-test/program"
with tempfile.TemporaryDirectory(prefix="valve-test-prefs-") as prefs:
    run(str(binary), env={**os.environ, "ESPHOME_PREFDIR": prefs})
