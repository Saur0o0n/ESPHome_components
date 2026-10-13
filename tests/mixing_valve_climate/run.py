"""Run schema checks and deterministic integration tests using ESPHome."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def run(*args, **kwargs):
    subprocess.run(args, cwd=ROOT, check=True, **kwargs)

run(sys.executable, "tests/mixing_valve_climate/test_schema.py")
run(sys.executable, "tests/mixing_valve_climate/test_ha_template.py")
run(sys.executable, "-m", "esphome", "compile", "tests/mixing_valve_climate/host.yaml")
binary = ROOT / "tests/mixing_valve_climate/.esphome/build/mixing-climate-test/.pioenvs/mixing-climate-test/program"
with tempfile.TemporaryDirectory(prefix="mixing-climate-prefs-") as prefs:
    run(str(binary), env={**os.environ, "ESPHOME_PREFDIR": prefs}, timeout=30)
