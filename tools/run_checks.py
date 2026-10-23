#!/usr/bin/env python3
"""Run structural regression checks and, when configured, a real P4 build.

Usage:
  python tools/run_checks.py                 # tests + build if available
  python tools/run_checks.py --build         # tests + build; fail if pio.ini/PIO absent
  python tools/run_checks.py --no-build      # structural tests only
  python tools/run_checks.py --env home_display_p4_r3_prod --build
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run(args, cwd=ROOT, check=False).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="Require actual PlatformIO compilation")
    parser.add_argument("--no-build", action="store_true", help="Run checks without PlatformIO")
    parser.add_argument("--env", default="home_display_p4_legacy_dev", help="PlatformIO environment")
    opts = parser.parse_args()
    if opts.build and opts.no_build:
        parser.error("--build and --no-build are mutually exclusive")

    if run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"]):
        return 1

    verifier = ROOT / "tools" / "verify_project.py"
    if verifier.is_file() and run([sys.executable, str(verifier)]):
        return 1

    if opts.no_build:
        print("PASS: structural checks only; firmware was not compiled")
        return 0

    if not (ROOT / "platformio.ini").is_file():
        if opts.build:
            print("FAIL: platformio.ini missing. Run in the complete firmware project.", file=sys.stderr)
            return 2
        print("SKIP: no platformio.ini (source archive only); firmware was not compiled")
        return 0

    from dev import build
    if build(opts.env):
        print("FAIL: PlatformIO compilation/linking failed", file=sys.stderr)
        return 1
    print("PASS: regression tests, project checks and firmware compilation/linking")
    return 0


if __name__ == "__main__":
    sys.exit(main())
