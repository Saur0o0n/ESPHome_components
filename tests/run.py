"""Discover and run component test suites; optionally select component names."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUITES = {p.parent.name: p for p in sorted((ROOT / "tests").glob("*/run.py"))}


def main():
    selected = sys.argv[1:] or list(SUITES)
    unknown = set(selected) - SUITES.keys()
    if unknown:
        raise SystemExit("Unknown components: " + ", ".join(sorted(unknown)))
    if not selected:
        raise SystemExit("No component test suites found")
    for name in selected:
        print(f"Testing component: {name}", flush=True)
        subprocess.run([sys.executable, str(SUITES[name])], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
